#include "mainwindow.h"
#include "topologynodeitem.h"
#include "topologylinkitem.h"

#include <QDateTime>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QUdpSocket>
#include <QVBoxLayout>
#include <algorithm>

namespace {
constexpr qint64 kDefaultStaleMs = 5000;
constexpr qint64 kLinkHoldMs = 3000;
constexpr qint64 kAckNackMinIntervalMs = 500;
constexpr int kMaxAckNackSeqs = 8;

struct NodeInit { QString id, label; TopologyNodeType type; QPointF pos; };
struct EndpointAttach { QString endpoint, sw, endpointPort, swPort; };

QString norm(const QString& id) { return id.trimmed().toLower(); }
QString ep(const QString& sw, const QString& p) { return sw + ":" + p; }
QString ckey(const QString& a, const QString& ap, const QString& b, const QString& bp)
{
    QString x = ep(a, ap), y = ep(b, bp);
    return x <= y ? x + "||" + y : y + "||" + x;
}
QString spair(const QString& a, const QString& b) { return a <= b ? a + "||" + b : b + "||" + a; }

const QSet<QString>& allowedIslPairs()
{
    static const QSet<QString> s = {
        "s1||s2", "s1||s3", "s2||s3", "s2||s4", "s2||s5",
        "s3||s4", "s3||s5", "s4||s5", "s4||s6", "s5||s6"
    };
    return s;
}

QString stxt(TopologyLinkStatus s)
{
    if (s == TopologyLinkStatus::Confirmed) return "confirmed";
    if (s == TopologyLinkStatus::Pending) return "pending";
    return "down";
}

const QStringList& switchIds()
{
    static const QStringList s = { "s1", "s2", "s3", "s4", "s5", "s6" };
    return s;
}

const QStringList& endpointIds()
{
    static const QStringList s = { "pc1", "pc2", "pc3", "pc4", "pc5", "pc6" };
    return s;
}

const QList<NodeInit>& nodes()
{
    static const QList<NodeInit> n = {
        { "pc2", "pc2", TopologyNodeType::Host, { 470, 70 } },
        { "pc4", "pc4", TopologyNodeType::Host, { 950, 70 } },
        { "pc1", "pc1", TopologyNodeType::Host, { 60, 330 } },
        { "s1",  "s1",  TopologyNodeType::Switch, { 220, 330 } },
        { "s2",  "s2",  TopologyNodeType::Switch, { 500, 190 } },
        { "s3",  "s3",  TopologyNodeType::Switch, { 500, 490 } },
        { "s4",  "s4",  TopologyNodeType::Switch, { 860, 190 } },
        { "s5",  "s5",  TopologyNodeType::Switch, { 860, 490 } },
        { "s6",  "s6",  TopologyNodeType::Switch, { 1140, 330 } },
        { "pc6", "pc6", TopologyNodeType::Host, { 1300, 330 } },
        { "pc3", "pc3", TopologyNodeType::Host, { 470, 650 } },
        { "pc5", "pc5", TopologyNodeType::Host, { 950, 650 } }
    };
    return n;
}

const QList<EndpointAttach>& endpoints()
{
    static const QList<EndpointAttach> h = {
        { "pc1", "s1", "pc1-eth", "pc1-sw" },
        { "pc2", "s2", "pc2-eth", "pc2-sw" },
        { "pc3", "s3", "pc3-eth", "pc3-sw" },
        { "pc4", "s4", "pc4-eth", "pc4-sw" },
        { "pc5", "s5", "pc5-eth", "pc5-sw" },
        { "pc6", "s6", "pc6-eth", "pc6-sw" }
    };
    return h;
}
}

MainWindow::MainWindow(const QHostAddress& listenHost, quint16 port, QWidget* parent)
    : QMainWindow(parent)
{
    buildUi();
    m_udp = new QUdpSocket(this);
    if (!m_udp->bind(listenHost, port, QUdpSocket::ShareAddress)) {
        appendLog(QString("center UDP bind failed, addr=%1 port=%2 error=%3")
                      .arg(listenHost.toString())
                      .arg(port)
                      .arg(m_udp->errorString()));
    }
    connect(m_udp, &QUdpSocket::readyRead, this, &MainWindow::processDatagrams);

    m_maintenanceTimer = new QTimer(this);
    connect(m_maintenanceTimer, &QTimer::timeout, this, &MainWindow::maintenanceTick);
    m_maintenanceTimer->start(100);

    m_graphTimer = new QTimer(this);
    connect(m_graphTimer, &QTimer::timeout, this, &MainWindow::renderGraphIfDirty);
    m_graphTimer->start(100);

    m_tableTimer = new QTimer(this);
    connect(m_tableTimer, &QTimer::timeout, this, &MainWindow::renderTableIfDirty);
    m_tableTimer->start(500);

    ensureFixedNodes();
    appendLog(QString("FC topology center listening RTPS-Lite UDP %1:%2")
                  .arg(listenHost.toString())
                  .arg(port));
}

void MainWindow::buildUi()
{
    resize(1500, 920);
    auto* c = new QWidget(this);
    auto* l = new QVBoxLayout(c);
    m_status = new QLabel("waiting FC topology...", this);
    l->addWidget(m_status);

    auto* sp = new QSplitter(Qt::Vertical, this);
    m_scene = new QGraphicsScene(this);
    m_scene->setSceneRect(0, 0, 1420, 780);
    m_view = new QGraphicsView(m_scene, this);
    m_view->setMinimumHeight(620);
    m_view->setStyleSheet("background:white;");
    m_view->setRenderHint(QPainter::Antialiasing, true);
    sp->addWidget(m_view);

    auto* b = new QWidget(this);
    auto* bl = new QVBoxLayout(b);
    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({ "Type", "Local FC Node", "Local FC Port", "Remote FC Node", "Remote FC Port", "Status" });
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setMaximumHeight(220);
    bl->addWidget(m_table);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(140);
    bl->addWidget(m_log);
    sp->addWidget(b);
    sp->setSizes({ 680, 240 });
    l->addWidget(sp);

    setCentralWidget(c);
    setWindowTitle("FC Topology Management Center");
}

void MainWindow::appendLog(const QString& t)
{
    m_log->appendPlainText("[" + QDateTime::currentDateTime().toString("HH:mm:ss.zzz") + "] " + t);
}

void MainWindow::ensureFixedNodes()
{
    for (const auto& n : nodes()) {
        if (m_nodeItems.contains(n.id)) continue;
        auto* item = new TopologyNodeItem(n.id, n.label, n.type);
        item->setPos(n.pos);
        m_scene->addItem(item);
        m_nodeItems.insert(n.id, item);
    }
}

void MainWindow::upsertNeighbor(Snapshot& s, const FcNeighborInfo& i)
{
    for (auto& n : s.fcNeighbors) {
        if (n.localPort == i.localPort) { n = i; return; }
    }
    s.fcNeighbors.push_back(i);
}

void MainWindow::removeNeighbor(Snapshot& s, const QString& localPort)
{
    auto it = std::remove_if(s.fcNeighbors.begin(), s.fcNeighbors.end(), [&](const FcNeighborInfo& n) {
        return n.localPort == localPort;
    });
    s.fcNeighbors.erase(it, s.fcNeighbors.end());
}

void MainWindow::upsertEndpointPort(Snapshot& s, const EndpointPortInfo& i)
{
    for (auto& p : s.endpointPorts) {
        if (p.portName == i.portName) { p = i; return; }
    }
    s.endpointPorts.push_back(i);
}

void MainWindow::upsertFcPort(Snapshot& s, const FcPortInfo& i)
{
    for (auto& p : s.fcPorts) {
        if (p.portName == i.portName) { p = i; return; }
    }
    s.fcPorts.push_back(i);
}

void MainWindow::applySnapshotPayload(const QJsonObject& o, const rtpslite::QoSProfile* q)
{
    if (o["topic"].toString() != "fc.topology.snapshot") return;

    Snapshot s;
    s.fcSwitchId = norm(o["fc_switch_id"].toString());
    s.ovsNode = norm(o["ovs_node"].toString());
    s.wwnn = o["wwnn"].toString();
    s.ts = o["timestamp_ms"].toString().toLongLong();
    s.recvMs = QDateTime::currentMSecsSinceEpoch();
    if (q) { s.deadlineMs = q->deadlineMs; s.livelinessMs = q->livelinessMs; }
    if (s.fcSwitchId.isEmpty()) return;

    auto old = m_snaps.constFind(s.fcSwitchId);
    if (old != m_snaps.constEnd() && old.value().ts > 0 && s.ts > 0 && s.ts < old.value().ts) return;

    for (const auto& v : o["fc_ports"].toArray()) {
        auto p = v.toObject();
        s.fcPorts.push_back({ p["port_name"].toString(), p["wwpn"].toString(), p["fc_id"].toString(),
                              p["role"].toString(), p["oper_up"].toBool(true),
                              p["last_change_ms"].toString().toLongLong() });
    }

    for (const auto& v : o["fc_neighbors"].toArray()) {
        auto n = v.toObject();
        s.fcNeighbors.push_back({ n["local_port"].toString(), n["local_wwpn"].toString(), n["local_fc_id"].toString(),
                                  norm(n["remote_switch_id"].toString()), n["remote_wwnn"].toString(),
                                  n["remote_port"].toString(), n["remote_wwpn"].toString(), n["remote_fc_id"].toString() });
    }

    for (const auto& v : o["endpoint_ports"].toArray()) {
        auto e = v.toObject();
        s.endpointPorts.push_back({ e["port_name"].toString(), norm(e["endpoint_id"].toString()), e["role"].toString(),
                                    e["admin_up"].toBool(), e["oper_up"].toBool(),
                                    e["last_seen_ms"].toString().toLongLong() });
    }

    m_snaps[s.fcSwitchId] = s;
    if (!s.wwnn.isEmpty()) m_wwnnToSwitch[s.wwnn] = s.fcSwitchId;
}

void MainWindow::applyDeltaPayload(const QJsonObject& o, const rtpslite::QoSProfile* q)
{
    if (o["topic"].toString() != "fc.topology.delta") return;

    QString sid = norm(o["fc_switch_id"].toString());
    if (sid.isEmpty()) return;
    auto& snap = m_snaps[sid];

    qint64 ts = o["timestamp_ms"].toString().toLongLong();
    if (!snap.fcSwitchId.isEmpty() && snap.ts > 0 && ts > 0 && ts < snap.ts) return;

    if (snap.fcSwitchId.isEmpty()) snap.fcSwitchId = sid;
    if (!o["ovs_node"].toString().isEmpty()) snap.ovsNode = norm(o["ovs_node"].toString());
    if (!o["wwnn"].toString().isEmpty()) snap.wwnn = o["wwnn"].toString();
    if (q) { snap.deadlineMs = q->deadlineMs; snap.livelinessMs = q->livelinessMs; }
    snap.ts = ts;
    snap.recvMs = QDateTime::currentMSecsSinceEpoch();
    if (!snap.wwnn.isEmpty()) m_wwnnToSwitch[snap.wwnn] = sid;

    for (const auto& v : o["events"].toArray()) {
        auto e = v.toObject();
        QString t = e["event_type"].toString();
        if (t == "fc_neighbor_set") {
            upsertNeighbor(snap, { e["local_port"].toString(), e["local_wwpn"].toString(), e["local_fc_id"].toString(),
                                   norm(e["remote_switch_id"].toString()), e["remote_wwnn"].toString(),
                                   e["remote_port"].toString(), e["remote_wwpn"].toString(), e["remote_fc_id"].toString() });
        } else if (t == "fc_neighbor_remove") {
            removeNeighbor(snap, e["local_port"].toString());
        } else if (t == "fc_endpoint_state") {
            upsertEndpointPort(snap, { e["port_name"].toString(), norm(e["endpoint_id"].toString()), "endpoint",
                                       e["admin_up"].toBool(), e["oper_up"].toBool(),
                                       e["last_seen_ms"].toString().toLongLong() });
        } else if (t == "fc_port_state") {
            upsertFcPort(snap, { e["port_name"].toString(), e["wwpn"].toString(), e["fc_id"].toString(), "isl",
                                 e["oper_up"].toBool(true), e["last_change_ms"].toString().toLongLong() });
        }
    }
}

bool MainWindow::isFresh(qint64 t, quint32 age) const
{
    return t <= 0 || age == 0 || QDateTime::currentMSecsSinceEpoch() - t <= qint64(age);
}

void MainWindow::trackDataSeq(const rtpslite::DataMessage& d, const QHostAddress& s, quint16 p)
{
    auto& st = m_writerRecv[d.writerId];
    st.lastAddr = s;
    st.lastPort = p;
    st.readerId = d.readerId;
    quint64 seq = d.writerSeqNum;
    if (st.expectedSeq == 0) { st.expectedSeq = seq + 1; st.received.insert(seq); return; }
    if (seq == st.expectedSeq) {
        st.received.insert(seq); st.missing.remove(seq); st.expectedSeq++;
        while (st.received.contains(st.expectedSeq)) { st.missing.remove(st.expectedSeq); st.expectedSeq++; }
    } else if (seq > st.expectedSeq) {
        for (quint64 x = st.expectedSeq; x < seq; ++x) st.missing.insert(x);
        st.received.insert(seq);
    } else {
        st.received.insert(seq); st.missing.remove(seq);
    }
    if (!st.missing.isEmpty()) sendAckNack(d.writerId);
}

void MainWindow::handleHeartbeat(const rtpslite::HeartbeatMessage& hb, const QHostAddress& s, quint16 p)
{
    auto& st = m_writerRecv[hb.writerId];
    st.lastAddr = s;
    st.lastPort = p;
    if (st.expectedSeq == 0 && hb.firstSeqNum > 0) st.expectedSeq = hb.firstSeqNum;
    if (st.expectedSeq > 0 && hb.lastSeqNum >= st.expectedSeq) {
        for (quint64 x = st.expectedSeq; x <= hb.lastSeqNum; ++x)
            if (!st.received.contains(x)) st.missing.insert(x);
        sendAckNack(hb.writerId);
    }
}

void MainWindow::sendAckNack(quint32 wid)
{
    auto it = m_writerRecv.find(wid);
    if (it == m_writerRecv.end() || it->missing.isEmpty()) return;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - it->lastAckNackMs < kAckNackMinIntervalMs) return;
    QList<quint64> missing = it->missing.values();
    std::sort(missing.begin(), missing.end());
    if (missing.size() > kMaxAckNackSeqs) missing = missing.mid(0, kMaxAckNackSeqs);

    rtpslite::AckNackMessage a;
    a.guidPrefix = rtpslite::makeGuidPrefixFromFcName(1, 2, "FC_TOPOLOGY_CENTRE", 1);
    a.readerId = it->readerId;
    a.writerId = wid;
    a.requestedSeqNums = missing;
    a.count = quint32(now & 0xffffffff);
    a.timestampMs = now;
    m_udp->writeDatagram(rtpslite::serializeAckNack(a), it->lastAddr, it->lastPort);
    it->lastAckNackMs = now;
}

void MainWindow::processDatagrams()
{
    bool updated = false;
    while (m_udp->hasPendingDatagrams()) {
        QByteArray dg;
        dg.resize(int(m_udp->pendingDatagramSize()));
        QHostAddress sender;
        quint16 port = 0;
        m_udp->readDatagram(dg.data(), dg.size(), &sender, &port);

        rtpslite::ParsedMessage pm;
        if (!rtpslite::parseMessage(dg, &pm)) continue;
        if (pm.kind == rtpslite::ParsedKind::Data) {
            trackDataSeq(pm.data, sender, port);
            if (!isFresh(pm.data.timestampMs, pm.data.qos.maxAgeMs)) continue;
            auto doc = QJsonDocument::fromJson(pm.data.payload);
            if (!doc.isObject()) continue;
            auto obj = doc.object();
            const QString topic = obj["topic"].toString();
            if (topic == "fc.topology.snapshot") {
                applySnapshotPayload(obj, &pm.data.qos);
                appendLog(QString("RTPS DATA snapshot from %1:%2 size=%3 seq=%4")
                              .arg(sender.toString())
                              .arg(port)
                              .arg(dg.size())
                              .arg(pm.data.writerSeqNum));
                updated = true;
            } else if (topic == "fc.topology.delta") {
                applyDeltaPayload(obj, &pm.data.qos);
                appendLog(QString("RTPS DATA delta from %1:%2 size=%3 seq=%4")
                              .arg(sender.toString())
                              .arg(port)
                              .arg(dg.size())
                              .arg(pm.data.writerSeqNum));
                updated = true;
            }
        } else if (pm.kind == rtpslite::ParsedKind::Heartbeat) {
            handleHeartbeat(pm.heartbeat, sender, port);
        }
    }
    if (updated) m_modelDirty = true;
}

QString MainWindow::buildGraphSignature() const
{
    QStringList parts;
    parts << m_cachedOnlineSwitches.join(",");
    QStringList epStates;
    for (const auto& id : endpointIds()) epStates << QString("%1=%2").arg(id).arg(m_cachedEndpointOnlineMap.value(id, false) ? 1 : 0);
    parts << epStates.join(",");
    QStringList links;
    for (auto it = m_fcIslLinkStates.begin(); it != m_fcIslLinkStates.end(); ++it) {
        const auto& s = it.value();
        links << QString("%1|%2|%3|%4|%5").arg(s.aSw, s.aPort, s.bSw, s.bPort, stxt(s.displayStatus));
    }
    std::sort(links.begin(), links.end());
    parts << links.join(";");
    return parts.join("||");
}

QString MainWindow::buildTableSignature() const { return buildGraphSignature(); }

bool MainWindow::isEndpointPortDown(const QString& sw, const QString& port) const
{
    auto it = m_snaps.constFind(sw);
    if (it == m_snaps.constEnd()) return false;
    for (const auto& p : it->fcPorts) if (p.portName == port) return !p.operUp;
    return false;
}

void MainWindow::rebuildModel()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_wwnnToSwitch.clear();
    for (auto it = m_snaps.begin(); it != m_snaps.end(); ++it) {
        if (!it.value().wwnn.isEmpty()) m_wwnnToSwitch[it.value().wwnn] = it.key();
    }

    QStringList online;
    for (auto it = m_snaps.begin(); it != m_snaps.end(); ++it) {
        const auto& s = it.value();
        qint64 live = qMax<qint64>(s.livelinessMs, qMax<qint64>(s.deadlineMs, kDefaultStaleMs));
        if (s.recvMs > 0 && now - s.recvMs <= live) online << it.key();
    }
    online.sort();

    QHash<QString, LinkInfo> cur;
    for (const auto& sw : online) {
        const auto& s = m_snaps[sw];
        for (const auto& n : s.fcNeighbors) {
            QString rs = norm(n.remoteSwitchId);
            if (rs.isEmpty() && !n.remoteWwnn.isEmpty()) rs = m_wwnnToSwitch.value(n.remoteWwnn);
            rs = norm(rs);
            if (rs.isEmpty() || !allowedIslPairs().contains(spair(sw, rs))) continue;
            QString le = ep(sw, n.localPort), re = ep(rs, n.remotePort);
            QString key = ckey(sw, n.localPort, rs, n.remotePort);
            auto& l = cur[key];
            if (le <= re) { l.aSw = sw; l.aPort = n.localPort; l.bSw = rs; l.bPort = n.remotePort; l.ab = true; }
            else { l.aSw = rs; l.aPort = n.remotePort; l.bSw = sw; l.bPort = n.localPort; l.ba = true; }
        }
    }

    for (auto it = cur.begin(); it != cur.end(); ++it) {
        const auto& r = it.value();
        auto& st = m_fcIslLinkStates[it.key()];
        st.aSw = r.aSw; st.aPort = r.aPort; st.bSw = r.bSw; st.bPort = r.bPort;
        st.lastSeenMs = now;
        bool down = isEndpointPortDown(r.aSw, r.aPort) || isEndpointPortDown(r.bSw, r.bPort);
        if (down) { st.displayStatus = TopologyLinkStatus::Down; continue; }
        if (r.ab && r.ba) { st.lastConfirmedMs = now; st.displayStatus = TopologyLinkStatus::Confirmed; }
        else if (st.lastConfirmedMs > 0 && now - st.lastConfirmedMs <= kLinkHoldMs) st.displayStatus = TopologyLinkStatus::Confirmed;
        else st.displayStatus = TopologyLinkStatus::Pending;
    }

    for (auto it = m_fcIslLinkStates.begin(); it != m_fcIslLinkStates.end(); ++it) {
        if (cur.contains(it.key())) continue;
        auto& st = it.value();
        bool down = isEndpointPortDown(st.aSw, st.aPort) || isEndpointPortDown(st.bSw, st.bPort);
        if (down) st.displayStatus = TopologyLinkStatus::Down;
        else if (st.lastConfirmedMs > 0 && now - st.lastConfirmedMs <= kLinkHoldMs) st.displayStatus = TopologyLinkStatus::Confirmed;
        else st.displayStatus = TopologyLinkStatus::Down;
    }

    QHash<QString, bool> endpointMap;
    for (const auto& sw : online) {
        const auto& s = m_snaps[sw];
        for (const auto& e : s.endpointPorts) {
            if (e.endpointId.isEmpty()) continue;
            endpointMap[e.endpointId] = endpointMap.value(e.endpointId, false) || (e.adminUp && e.operUp);
        }
    }

    int confirmedLinks = 0;
    for (const auto& l : m_fcIslLinkStates) if (l.displayStatus == TopologyLinkStatus::Confirmed) confirmedLinks++;

    m_cachedOnlineSwitches = online;
    m_cachedEndpointOnlineMap = endpointMap;
    m_cachedVisibleIslLinks = confirmedLinks;

    QString gs = buildGraphSignature(), ts = buildTableSignature();
    if (gs != m_lastGraphSignature) { m_lastGraphSignature = gs; m_graphDirty = true; }
    if (ts != m_lastTableSignature) { m_lastTableSignature = ts; m_tableDirty = true; }
}

void MainWindow::maintenanceTick()
{
    if (!m_modelDirty) return;
    rebuildModel();
    m_modelDirty = false;
}

void MainWindow::updateSwitchNodes(const QStringList& o)
{
    for (const auto& id : switchIds()) if (m_nodeItems.contains(id)) m_nodeItems[id]->setOnline(o.contains(id));
}

void MainWindow::updateEndpointNodes(const QHash<QString, bool>& m)
{
    for (const auto& id : endpointIds()) if (m_nodeItems.contains(id)) m_nodeItems[id]->setOnline(m.value(id, false));
}

QString MainWindow::makeLinkKey(const QString& a, const QString& ap, const QString& b, const QString& bp) const
{
    return ckey(a, ap, b, bp);
}

void MainWindow::updateLinks(const QHash<QString, bool>& endpointMap)
{
    QHash<QString, bool> alive;
    for (auto it = m_fcIslLinkStates.begin(); it != m_fcIslLinkStates.end(); ++it) {
        const auto& s = it.value();
        alive[it.key()] = true;
        auto* a = m_nodeItems.value(s.aSw, nullptr);
        auto* b = m_nodeItems.value(s.bSw, nullptr);
        if (!a || !b) continue;
        QString label = QString("%1:%2 ↔ %3:%4").arg(s.aSw, s.aPort, s.bSw, s.bPort);
        if (!m_linkItems.contains(it.key())) {
            auto* link = new TopologyLinkItem(a, b, label);
            link->setStatus(s.displayStatus);
            m_scene->addItem(link);
            m_linkItems.insert(it.key(), link);
        } else {
            m_linkItems[it.key()]->setLabel(label);
            m_linkItems[it.key()]->setStatus(s.displayStatus);
            m_linkItems[it.key()]->show();
        }
    }

    for (const auto& e : endpoints()) {
        QString key = makeLinkKey(e.endpoint, e.endpointPort, e.sw, e.swPort);
        alive[key] = true;
        auto* ei = m_nodeItems.value(e.endpoint, nullptr);
        auto* si = m_nodeItems.value(e.sw, nullptr);
        if (!ei || !si) continue;
        TopologyLinkStatus st = endpointMap.value(e.endpoint, false) ? TopologyLinkStatus::Confirmed : TopologyLinkStatus::Down;
        QString label = QString("%1 ↔ %2").arg(e.endpoint, e.sw);
        if (!m_linkItems.contains(key)) {
            auto* link = new TopologyLinkItem(ei, si, label);
            link->setStatus(st);
            m_scene->addItem(link);
            m_linkItems.insert(key, link);
        } else {
            m_linkItems[key]->setLabel(label);
            m_linkItems[key]->setStatus(st);
            m_linkItems[key]->show();
        }
    }

    for (auto it = m_linkItems.begin(); it != m_linkItems.end(); ++it)
        if (!alive.contains(it.key())) it.value()->hide();
}

void MainWindow::updateStatusText()
{
    int epOnline = 0;
    for (const auto& id : endpointIds()) if (m_cachedEndpointOnlineMap.value(id, false)) epOnline++;
    m_status->setText(QString("online_fc_switches=%1/%2  fc_isl_links=%3  online_endpoints=%4/%5  writers=%6  updated=%7")
                          .arg(m_cachedOnlineSwitches.size()).arg(switchIds().size())
                          .arg(m_cachedVisibleIslLinks)
                          .arg(epOnline).arg(endpointIds().size())
                          .arg(m_writerRecv.size())
                          .arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz")));
}

void MainWindow::renderGraphIfDirty()
{
    if (!m_graphDirty) return;
    updateSwitchNodes(m_cachedOnlineSwitches);
    updateEndpointNodes(m_cachedEndpointOnlineMap);
    updateLinks(m_cachedEndpointOnlineMap);
    updateStatusText();
    m_graphDirty = false;
}

void MainWindow::renderTableIfDirty()
{
    if (!m_tableDirty) return;
    QList<StableLinkState> links = m_fcIslLinkStates.values();
    std::sort(links.begin(), links.end(), [](const StableLinkState& a, const StableLinkState& b) {
        return a.aSw + a.aPort + a.bSw + a.bPort < b.aSw + b.aPort + b.bSw + b.bPort;
    });
    m_table->clearContents();
    m_table->setRowCount(links.size() + endpoints().size());
    int row = 0;
    for (const auto& l : links) {
        m_table->setItem(row, 0, new QTableWidgetItem("fc_isl_link"));
        m_table->setItem(row, 1, new QTableWidgetItem(l.aSw));
        m_table->setItem(row, 2, new QTableWidgetItem(l.aPort));
        m_table->setItem(row, 3, new QTableWidgetItem(l.bSw));
        m_table->setItem(row, 4, new QTableWidgetItem(l.bPort));
        m_table->setItem(row, 5, new QTableWidgetItem(stxt(l.displayStatus)));
        row++;
    }
    for (const auto& e : endpoints()) {
        bool on = m_cachedEndpointOnlineMap.value(e.endpoint, false);
        m_table->setItem(row, 0, new QTableWidgetItem("fc_endpoint_access"));
        m_table->setItem(row, 1, new QTableWidgetItem(e.endpoint));
        m_table->setItem(row, 2, new QTableWidgetItem(e.endpointPort));
        m_table->setItem(row, 3, new QTableWidgetItem(e.sw));
        m_table->setItem(row, 4, new QTableWidgetItem(e.swPort));
        m_table->setItem(row, 5, new QTableWidgetItem(on ? "online" : "offline"));
        row++;
    }
    m_tableDirty = false;
}
