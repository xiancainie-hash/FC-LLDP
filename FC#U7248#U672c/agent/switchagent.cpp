#include "switchagent.h"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QProcess>
#include <QDebug>
#include <algorithm>

namespace {
constexpr int kDiscoveryTtlSec = 4;
constexpr int kNeighborMissedConfirm = 2;
constexpr int kDownConfirmSamples = 5;
constexpr int kUpConfirmSamples = 3;
constexpr int kMaxRetransmitPerSeq = 2;
constexpr quint16 kDomainId = 1;
constexpr quint16 kRoleAgent = 1;
}

SwitchAgent::SwitchAgent(const QString& bridge,
                         const QString& switchId,
                         const QHostAddress& centerHost,
                         quint16 centerPort,
                         const QHostAddress& rtpsLocalHost,
                         QObject* parent)
    : QObject(parent)
    , m_bridge(bridge)
    , m_switchId(switchId)
    , m_centerHost(centerHost)
    , m_centerPort(centerPort)
    , m_rtpsLocalHost(rtpsLocalHost)
{
    connect(&m_udp, &QUdpSocket::readyRead, this, &SwitchAgent::processControlDatagrams);
    connect(&m_txTimer, &QTimer::timeout, this, &SwitchAgent::txDiscovery);
    connect(&m_rxTimer, &QTimer::timeout, this, &SwitchAgent::rxDiscovery);
    connect(&m_expireTimer, &QTimer::timeout, this, &SwitchAgent::expireNeighbors);
    connect(&m_snapshotTimer, &QTimer::timeout, this, &SwitchAgent::sendPeriodicSnapshot);
    connect(&m_bootstrapTimer, &QTimer::timeout, this, &SwitchAgent::sendBootstrapSnapshot);
    connect(&m_heartbeatTimer, &QTimer::timeout, this, &SwitchAgent::sendHeartbeat);
    connect(&m_endpointScanTimer, &QTimer::timeout, this, &SwitchAgent::scanEndpointPorts);
    connect(&m_fcPortScanTimer, &QTimer::timeout, this, &SwitchAgent::scanFcPorts);

    m_deltaDebounceTimer.setSingleShot(true);
    connect(&m_deltaDebounceTimer, &QTimer::timeout, this, &SwitchAgent::sendPendingDelta);
}

SwitchAgent::~SwitchAgent()
{
    for (auto it = m_fcPorts.begin(); it != m_fcPorts.end(); ++it) {
        if (it->pcap) {
            pcap_close(it->pcap);
            it->pcap = nullptr;
        }
    }
}

QString SwitchAgent::runCmd(const QString& prog, const QStringList& args) const
{
    QProcess p;
    p.start(prog, args);
    p.waitForFinished(2000);
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

QString SwitchAgent::getExternalId(const QString& ifName, const QString& key) const
{
    QString v = runCmd("ovs-vsctl", {"get", "Interface", ifName, QString("external_ids:%1").arg(key)});
    if (v == "[]") return {};
    if (v.startsWith('"') && v.endsWith('"')) v = v.mid(1, v.size() - 2);
    return v.trimmed();
}

QString SwitchAgent::getRole(const QString& ifName) const
{
    QString v = getExternalId(ifName, "fc_role");
    if (v.isEmpty()) v = getExternalId(ifName, "lldp_role"); // 兼容旧脚本，建议后续删除 lldp_role
    return v;
}

bool SwitchAgent::isFcPortRole(const QString& role) const
{
    return role == "isl" || role == "uplink";
}

bool SwitchAgent::isEndpointRole(const QString& role) const
{
    return role == "endpoint" || role == "host";
}

QString SwitchAgent::deriveEndpointId(const QString& portName) const
{
    int pos = portName.indexOf('-');
    return pos > 0 ? portName.left(pos).toLower() : portName.toLower();
}

bool SwitchAgent::readOperUp(const QString& ifName) const
{
    QFile c(QString("/sys/class/net/%1/carrier").arg(ifName));
    if (c.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString v = QString::fromUtf8(c.readAll()).trimmed();
        if (v == "1") return true;
        if (v == "0") return false;
    }
    QFile s(QString("/sys/class/net/%1/operstate").arg(ifName));
    if (s.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString v = QString::fromUtf8(s.readAll()).trimmed();
        return v == "up" || v == "unknown";
    }
    return false;
}

bool SwitchAgent::readAdminUp(const QString& ifName) const
{
    auto iface = QNetworkInterface::interfaceFromName(ifName);
    return iface.isValid() && iface.flags().testFlag(QNetworkInterface::IsUp);
}

qint64 SwitchAgent::nowMs() const
{
    return QDateTime::currentMSecsSinceEpoch();
}

int SwitchAgent::switchIndex() const
{
    QString d;
    for (QChar c : m_switchId) if (c.isDigit()) d.append(c);
    bool ok = false;
    int v = d.toInt(&ok);
    return ok ? v : 0;
}

QString SwitchAgent::expectedRemotePortName(const QString& localPort) const
{
    QString peer = getExternalId(localPort, "fc_peer");
    if (!peer.isEmpty()) return peer;
    auto ps = localPort.split('-');
    if (ps.size() != 2) return {};
    return ps[1] + "-" + ps[0];
}

bool SwitchAgent::discoverFcPorts()
{
    QString out = runCmd("ovs-vsctl", {"list-ports", m_bridge});
    for (const QString& line : out.split('\n', Qt::SkipEmptyParts)) {
        QString p = line.trimmed();
        if (p.isEmpty() || !isFcPortRole(getRole(p))) continue;
        if (m_fcPorts.contains(p)) continue;

        char errbuf[PCAP_ERRBUF_SIZE] = {0};
        pcap_t* handle = pcap_open_live(p.toUtf8().constData(), 4096, 1, 20, errbuf);
        if (!handle) {
            qWarning() << "pcap_open_live failed on" << p << errbuf;
            continue;
        }

        struct bpf_program fp;
        QByteArray filter = QByteArray("ether proto 0x") + QByteArray::number(fcdisc::CARRIER_PROTO_FC_DISCOVERY, 16);
        if (pcap_compile(handle, &fp, filter.constData(), 1, PCAP_NETMASK_UNKNOWN) == 0) {
            if (pcap_setfilter(handle, &fp) != 0)
                qWarning() << "pcap_setfilter failed on" << p << pcap_geterr(handle);
            pcap_freecode(&fp);
        }
        if (pcap_setnonblock(handle, 1, errbuf) != 0) {
            qWarning() << "pcap_setnonblock failed on" << p << errbuf;
            pcap_close(handle);
            continue;
        }

        FcPortState st;
        st.portName = p;
        st.carrierAddress = QNetworkInterface::interfaceFromName(p).hardwareAddress();
        st.wwpn = fcdisc::makeWwpn(m_switchId, p);
        st.fcId = fcdisc::makeFcId(m_switchId, p);
        st.fcIdText = fcdisc::fcIdToString(st.fcId);
        st.role = "isl";
        st.pcap = handle;
        st.operUp = readOperUp(p);
        st.upSamples = st.operUp ? kUpConfirmSamples : 0;
        st.downSamples = st.operUp ? 0 : kDownConfirmSamples;
        m_fcPorts.insert(p, st);
    }
    return !m_fcPorts.isEmpty();
}

bool SwitchAgent::discoverEndpointPorts()
{
    QString out = runCmd("ovs-vsctl", {"list-ports", m_bridge});
    for (const QString& line : out.split('\n', Qt::SkipEmptyParts)) {
        QString p = line.trimmed();
        if (p.isEmpty() || !isEndpointRole(getRole(p))) continue;
        EndpointPortState ep;
        ep.portName = p;
        ep.endpointId = deriveEndpointId(p);
        m_endpointPorts.insert(p, ep);
    }
    return true;
}

QJsonArray SwitchAgent::refreshEndpointPortStates(bool emitEvents)
{
    qint64 now = nowMs();
    QJsonArray events;
    for (auto it = m_endpointPorts.begin(); it != m_endpointPorts.end(); ++it) {
        bool oldAdmin = it->adminUp;
        bool oldOper = it->operUp;
        qint64 oldLastSeen = it->lastSeenMs;
        it->adminUp = readAdminUp(it->portName);
        it->operUp = readOperUp(it->portName);
        if (it->operUp) it->lastSeenMs = now;
        if (emitEvents && (oldAdmin != it->adminUp || oldOper != it->operUp ||
                           (oldLastSeen == 0 && it->lastSeenMs > 0))) {
            QJsonObject evt;
            evt["event_type"] = "fc_endpoint_state";
            evt["port_name"] = it->portName;
            evt["endpoint_id"] = it->endpointId;
            evt["role"] = "endpoint";
            evt["admin_up"] = it->adminUp;
            evt["oper_up"] = it->operUp;
            evt["last_seen_ms"] = QString::number(it->lastSeenMs);
            events.append(evt);
        }
    }
    return events;
}

void SwitchAgent::initWriters()
{
    int idx = switchIndex();
    m_snapshotWriter.readerId = rtpslite::READER_SNAPSHOT;
    m_snapshotWriter.writerId = (quint32(idx) << 8) | 1;
    m_snapshotWriter.qos.reliability = quint32(rtpslite::ReliabilityKind::BestEffort);
    m_snapshotWriter.qos.historyDepth = 2;
    m_snapshotWriter.qos.maxAgeMs = 5000;
    m_snapshotWriter.qos.deadlineMs = 5000;
    m_snapshotWriter.qos.livelinessMs = 5000;
    m_snapshotWriter.qos.topicKind = quint32(rtpslite::TopicKind::Snapshot);

    m_deltaWriter.readerId = rtpslite::READER_DELTA;
    m_deltaWriter.writerId = (quint32(idx) << 8) | 2;
    m_deltaWriter.qos.reliability = quint32(rtpslite::ReliabilityKind::Reliable);
    m_deltaWriter.qos.historyDepth = 32;
    m_deltaWriter.qos.maxAgeMs = 3000;
    m_deltaWriter.qos.deadlineMs = 3000;
    m_deltaWriter.qos.livelinessMs = 5000;
    m_deltaWriter.qos.topicKind = quint32(rtpslite::TopicKind::Delta);
}

QJsonObject SwitchAgent::buildSnapshotObject() const
{
    QJsonObject root;
    root["topic"] = "fc.topology.snapshot";
    root["type"] = "snapshot";
    root["fc_switch_id"] = m_switchId;
    root["ovs_node"] = m_bridge;
    root["wwnn"] = m_wwnn;
    root["timestamp_ms"] = QString::number(nowMs());

    QJsonArray fcPorts;
    for (auto it = m_fcPorts.begin(); it != m_fcPorts.end(); ++it) {
        QJsonObject p;
        p["port_name"] = it->portName;
        p["wwpn"] = it->wwpn;
        p["fc_id"] = it->fcIdText;
        p["role"] = it->role;
        p["admin_up"] = readAdminUp(it->portName);
        p["oper_up"] = it->operUp;
        p["last_change_ms"] = QString::number(nowMs());
        fcPorts.append(p);
    }
    root["fc_ports"] = fcPorts;

    QJsonArray fcNeighbors;
    for (auto it = m_neighbors.begin(); it != m_neighbors.end(); ++it) {
        QJsonObject n;
        n["local_port"] = it->localPort;
        n["local_wwpn"] = it->localWwpn;
        n["local_fc_id"] = it->localFcIdText;
        n["remote_switch_id"] = it->remoteSwitchId;
        n["remote_wwnn"] = it->remoteWwnn;
        n["remote_port"] = it->remotePort;
        n["remote_wwpn"] = it->remoteWwpn;
        n["remote_fc_id"] = it->remoteFcIdText;
        fcNeighbors.append(n);
    }
    root["fc_neighbors"] = fcNeighbors;

    QJsonArray endpoints;
    for (auto it = m_endpointPorts.begin(); it != m_endpointPorts.end(); ++it) {
        QJsonObject e;
        e["port_name"] = it->portName;
        e["endpoint_id"] = it->endpointId;
        e["role"] = "endpoint";
        e["admin_up"] = it->adminUp;
        e["oper_up"] = it->operUp;
        e["last_seen_ms"] = QString::number(it->lastSeenMs);
        endpoints.append(e);
    }
    root["endpoint_ports"] = endpoints;
    return root;
}

void SwitchAgent::appendDeltaEvent(const QJsonObject& e)
{
    m_pendingDeltaEvents.append(e);
    requestDeltaSoon();
}
void SwitchAgent::requestDeltaSoon()
{
    if (!m_deltaDebounceTimer.isActive()) m_deltaDebounceTimer.start(100);
}
void SwitchAgent::sendPendingDelta()
{
    if (m_pendingDeltaEvents.isEmpty()) return;
    QJsonArray ev = m_pendingDeltaEvents;
    m_pendingDeltaEvents = QJsonArray();
    sendDelta(ev);
}
void SwitchAgent::sendSnapshot()
{
    refreshEndpointPortStates(false);
    sendData(m_snapshotWriter, buildSnapshotObject());
}
void SwitchAgent::sendDelta(const QJsonArray& events)
{
    if (events.isEmpty()) return;
    QJsonObject root;
    root["topic"] = "fc.topology.delta";
    root["fc_switch_id"] = m_switchId;
    root["ovs_node"] = m_bridge;
    root["wwnn"] = m_wwnn;
    root["timestamp_ms"] = QString::number(nowMs());
    root["events"] = events;
    sendData(m_deltaWriter, root);
}

QByteArray SwitchAgent::sendData(WriterState& w, const QJsonObject& obj)
{
    QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    rtpslite::DataMessage m;
    m.guidPrefix = m_guidPrefix;
    m.readerId = w.readerId;
    m.writerId = w.writerId;
    m.writerSeqNum = w.nextSeqNum++;
    m.timestampMs = nowMs();
    m.qos = w.qos;
    m.payload = payload;
    QByteArray dg = rtpslite::serializeData(m);
    m_udp.writeDatagram(dg, m_centerHost, m_centerPort);
    storeHistory(w, m.writerSeqNum, dg);
    return dg;
}

void SwitchAgent::storeHistory(WriterState& w, quint64 seq, const QByteArray& dg)
{
    int depth = int(w.qos.historyDepth);
    if (depth <= 0) return;
    w.history[seq] = dg;
    w.historyOrder.append(seq);
    w.retransmitCount[seq] = 0;
    while (w.historyOrder.size() > depth) {
        quint64 old = w.historyOrder.takeFirst();
        w.history.remove(old);
        w.retransmitCount.remove(old);
    }
}

SwitchAgent::WriterState* SwitchAgent::writerById(quint32 id)
{
    if (m_snapshotWriter.writerId == id) return &m_snapshotWriter;
    if (m_deltaWriter.writerId == id) return &m_deltaWriter;
    return nullptr;
}

void SwitchAgent::handleAckNack(const rtpslite::AckNackMessage& ack)
{
    WriterState* w = writerById(ack.writerId);
    if (!w) return;
    for (quint64 seq : ack.requestedSeqNums) {
        if (!w->history.contains(seq)) continue;
        int c = w->retransmitCount.value(seq, 0);
        if (c >= kMaxRetransmitPerSeq) continue;
        m_udp.writeDatagram(w->history.value(seq), m_centerHost, m_centerPort);
        w->retransmitCount[seq] = c + 1;
    }
}

bool SwitchAgent::start()
{
    auto br = QNetworkInterface::interfaceFromName(m_bridge);
    if (!br.isValid()) {
        qWarning() << "invalid bridge" << m_bridge;
        return false;
    }
    QHostAddress bindHost = m_rtpsLocalHost;
    if (bindHost.isNull()) {
        bindHost = QHostAddress(QHostAddress::AnyIPv4);
    }
    if (!m_udp.bind(bindHost, 0)) {
        qWarning() << "agent RTPS UDP bind failed"
                   << "bindHost=" << bindHost.toString()
                   << "error=" << m_udp.errorString();
        return false;
    }
    qInfo() << "[RTPS] UDP socket bound"
            << "local=" << m_udp.localAddress().toString() << m_udp.localPort()
            << "center=" << m_centerHost.toString() << m_centerPort;

    m_wwnn = fcdisc::makeWwnn(m_switchId);
    m_guidPrefix = rtpslite::makeGuidPrefixFromFcName(kDomainId, kRoleAgent, m_wwnn, 1);

    if (!discoverFcPorts()) {
        qWarning() << "no FC ISL ports on" << m_bridge;
        return false;
    }
    discoverEndpointPorts();
    refreshEndpointPortStates(false);
    initWriters();

    m_txTimer.start(800);
    m_rxTimer.start(50);
    m_expireTimer.start(300);
    m_snapshotTimer.start(3000);
    m_bootstrapRoundsRemaining = 2;
    m_bootstrapTimer.start(800);
    m_heartbeatTimer.start(1000);
    m_endpointScanTimer.start(300);
    m_fcPortScanTimer.start(100);

    txDiscovery();
    sendSnapshot();
    sendHeartbeat();
    return true;
}

void SwitchAgent::txDiscovery()
{
    for (auto it = m_fcPorts.begin(); it != m_fcPorts.end(); ++it) {
        if (!it->operUp || !it->pcap) continue;
        fcdisc::DiscoveryMessage m;
        m.switchId = m_switchId;
        m.localPort = it.key();
        m.wwnn = m_wwnn;
        m.wwpn = it->wwpn;
        m.portRole = it->role;
        m.srcFcId = it->fcId;
        m.dstFcId = 0xFFFFFF;
        m.seqNum = it->discoverySeq++;
        m.holdTimeSec = kDiscoveryTtlSec;
        m.capability = "fc_switch";
        m.systemDesc = QString("FC Discovery agent switch=%1 port=%2").arg(m_switchId, it.key());

        QByteArray src = fcdisc::macStringToBytes(it->carrierAddress);
        QByteArray frame = fcdisc::buildFcDiscoveryFrame(src, m);
        if (pcap_sendpacket(it->pcap, reinterpret_cast<const u_char*>(frame.constData()), frame.size()) != 0)
            qWarning() << "pcap_sendpacket failed on" << it.key() << pcap_geterr(it->pcap);
    }
}

void SwitchAgent::upsertNeighborFromDiscovery(const QString& ingressPort, const fcdisc::DiscoveryMessage& parsed)
{
    if (parsed.switchId == m_switchId || parsed.wwnn == m_wwnn) return;
    QString exp = expectedRemotePortName(ingressPort);
    if (!exp.isEmpty() && parsed.localPort != exp) return;

    const auto local = m_fcPorts.value(ingressPort);
    Neighbor n;
    n.localPort = ingressPort;
    n.localWwpn = local.wwpn;
    n.localFcIdText = local.fcIdText;
    n.remoteSwitchId = parsed.switchId;
    n.remoteWwnn = parsed.wwnn;
    n.remoteWwpn = parsed.wwpn;
    n.remoteFcIdText = fcdisc::fcIdToString(parsed.srcFcId);
    n.remotePort = parsed.localPort;
    n.expiresAt = nowMs() + qint64(parsed.holdTimeSec) * 1000;
    n.missedCount = 0;

    auto old = m_neighbors.value(ingressPort);
    bool changed = (old.remoteWwnn != n.remoteWwnn || old.remoteWwpn != n.remoteWwpn ||
                    old.remoteFcIdText != n.remoteFcIdText || old.remotePort != n.remotePort ||
                    old.remoteSwitchId != n.remoteSwitchId);
    m_neighbors[ingressPort] = n;

    if (changed) {
        QJsonObject e;
        e["event_type"] = "fc_neighbor_set";
        e["local_port"] = n.localPort;
        e["local_wwpn"] = n.localWwpn;
        e["local_fc_id"] = n.localFcIdText;
        e["remote_switch_id"] = n.remoteSwitchId;
        e["remote_wwnn"] = n.remoteWwnn;
        e["remote_port"] = n.remotePort;
        e["remote_wwpn"] = n.remoteWwpn;
        e["remote_fc_id"] = n.remoteFcIdText;
        appendDeltaEvent(e);
    }
}

void SwitchAgent::rxDiscovery()
{
    for (auto it = m_fcPorts.begin(); it != m_fcPorts.end(); ++it) {
        if (!it->operUp || !it->pcap) continue;
        while (true) {
            pcap_pkthdr* hdr = nullptr;
            const u_char* data = nullptr;
            int rc = pcap_next_ex(it->pcap, &hdr, &data);
            if (rc != 1) break;
            auto parsed = fcdisc::parseFcDiscoveryFrame(reinterpret_cast<const uint8_t*>(data), hdr->caplen);
            if (!parsed) continue;
            upsertNeighborFromDiscovery(it.key(), *parsed);
        }
    }
}

void SwitchAgent::expireNeighbors()
{
    qint64 now = nowMs();
    auto it = m_neighbors.begin();
    while (it != m_neighbors.end()) {
        if (it->expiresAt <= now) {
            it->missedCount++;
            if (it->missedCount >= kNeighborMissedConfirm) {
                QJsonObject e;
                e["event_type"] = "fc_neighbor_remove";
                e["local_port"] = it->localPort;
                e["local_wwpn"] = it->localWwpn;
                e["local_fc_id"] = it->localFcIdText;
                e["remote_switch_id"] = it->remoteSwitchId;
                e["remote_wwnn"] = it->remoteWwnn;
                e["remote_port"] = it->remotePort;
                e["remote_wwpn"] = it->remoteWwpn;
                e["remote_fc_id"] = it->remoteFcIdText;
                e["reason"] = "hold_time_expired";
                appendDeltaEvent(e);
                it = m_neighbors.erase(it);
                continue;
            } else {
                it->expiresAt = now + 300;
            }
        }
        ++it;
    }
}

void SwitchAgent::sendPeriodicSnapshot() { sendSnapshot(); }
void SwitchAgent::sendBootstrapSnapshot()
{
    if (m_bootstrapRoundsRemaining <= 0) { m_bootstrapTimer.stop(); return; }
    --m_bootstrapRoundsRemaining;
    sendSnapshot();
}

void SwitchAgent::sendHeartbeat()
{
    for (WriterState* w : { &m_snapshotWriter, &m_deltaWriter }) {
        rtpslite::HeartbeatMessage hb;
        hb.guidPrefix = m_guidPrefix;
        hb.writerId = w->writerId;
        hb.firstSeqNum = w->historyOrder.isEmpty() ? w->nextSeqNum : w->historyOrder.first();
        hb.lastSeqNum = w->nextSeqNum > 1 ? w->nextSeqNum - 1 : 0;
        hb.count = m_heartbeatCount++;
        hb.timestampMs = nowMs();
        m_udp.writeDatagram(rtpslite::serializeHeartbeat(hb), m_centerHost, m_centerPort);
    }
}

void SwitchAgent::scanEndpointPorts()
{
    auto ev = refreshEndpointPortStates(true);
    for (auto v : ev) appendDeltaEvent(v.toObject());
}

void SwitchAgent::scanFcPorts()
{
    for (auto it = m_fcPorts.begin(); it != m_fcPorts.end(); ++it) {
        bool nowUp = readOperUp(it->portName);
        if (nowUp) {
            it->upSamples++;
            it->downSamples = 0;
            if (!it->operUp && it->upSamples >= kUpConfirmSamples) {
                it->operUp = true;
                QJsonObject e;
                e["event_type"] = "fc_port_state";
                e["port_name"] = it->portName;
                e["wwpn"] = it->wwpn;
                e["fc_id"] = it->fcIdText;
                e["role"] = it->role;
                e["admin_up"] = readAdminUp(it->portName);
                e["oper_up"] = true;
                e["last_change_ms"] = QString::number(nowMs());
                appendDeltaEvent(e);
                txDiscovery();
            }
        } else {
            it->downSamples++;
            it->upSamples = 0;
            if (it->operUp && it->downSamples >= kDownConfirmSamples) {
                it->operUp = false;
                if (m_neighbors.contains(it->portName)) {
                    auto old = m_neighbors.value(it->portName);
                    QJsonObject e;
                    e["event_type"] = "fc_neighbor_remove";
                    e["local_port"] = old.localPort;
                    e["local_wwpn"] = old.localWwpn;
                    e["local_fc_id"] = old.localFcIdText;
                    e["remote_switch_id"] = old.remoteSwitchId;
                    e["remote_wwnn"] = old.remoteWwnn;
                    e["remote_port"] = old.remotePort;
                    e["remote_wwpn"] = old.remoteWwpn;
                    e["remote_fc_id"] = old.remoteFcIdText;
                    e["reason"] = "local_port_down";
                    appendDeltaEvent(e);
                    m_neighbors.remove(it->portName);
                }
                QJsonObject e;
                e["event_type"] = "fc_port_state";
                e["port_name"] = it->portName;
                e["wwpn"] = it->wwpn;
                e["fc_id"] = it->fcIdText;
                e["role"] = it->role;
                e["admin_up"] = readAdminUp(it->portName);
                e["oper_up"] = false;
                e["last_change_ms"] = QString::number(nowMs());
                appendDeltaEvent(e);
            }
        }
    }
}

void SwitchAgent::processControlDatagrams()
{
    while (m_udp.hasPendingDatagrams()) {
        QByteArray dg;
        dg.resize(int(m_udp.pendingDatagramSize()));
        QHostAddress s;
        quint16 p = 0;
        m_udp.readDatagram(dg.data(), dg.size(), &s, &p);
        qInfo() << "[RTPS] control datagram from" << s.toString() << p << "size=" << dg.size();
        rtpslite::ParsedMessage pm;
        if (!rtpslite::parseMessage(dg, &pm)) continue;
        if (pm.kind == rtpslite::ParsedKind::AckNack) handleAckNack(pm.acknack);
    }
}
