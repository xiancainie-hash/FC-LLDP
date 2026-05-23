#include "rtpslite.h"

#include <QDataStream>
#include <QIODevice>
#include <QCryptographicHash>

namespace rtpslite {

namespace {
constexpr char kProtocol[4] = { 'R', 'T', 'P', 'S' };
constexpr quint8 kMajor = 2;
constexpr quint8 kMinor = 4;
constexpr quint8 kVendor0 = 0x88;
constexpr quint8 kVendor1 = 0x66;
constexpr quint16 QOS_RELIABILITY = 1;
constexpr quint16 QOS_HISTORY = 2;
constexpr quint16 QOS_MAX_AGE = 3;
constexpr quint16 QOS_DEADLINE = 4;
constexpr quint16 QOS_LIVELINESS = 5;
constexpr quint16 QOS_TOPIC = 6;

QByteArray guidPrefix12(const QByteArray& input)
{
    QByteArray out = input.left(12);
    if (out.size() < 12) out.append(QByteArray(12 - out.size(), char(0)));
    return out;
}

void writeRtpsHeader(QDataStream& ds, const QByteArray& guidPrefix)
{
    ds.writeRawData(kProtocol, 4);
    ds << kMajor << kMinor << kVendor0 << kVendor1;
    QByteArray gp = guidPrefix12(guidPrefix);
    ds.writeRawData(gp.constData(), 12);
}

void writeSubHeader(QDataStream& ds, quint8 id, quint8 flags, quint16 len)
{
    ds << id << flags << len;
}

void writeInfoTs(QDataStream& ds, qint64 ms)
{
    writeSubHeader(ds, quint8(SubmessageId::InfoTs), 1, 8);
    ds << qint32(ms / 1000) << quint32((ms % 1000) * 1000000LL);
}

QByteArray qosBytes(const QoSProfile& q)
{
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    auto append = [&](quint16 id, quint32 value) { ds << id << quint16(4) << value; };
    append(QOS_RELIABILITY, q.reliability);
    append(QOS_HISTORY, q.historyDepth);
    append(QOS_MAX_AGE, q.maxAgeMs);
    append(QOS_DEADLINE, q.deadlineMs);
    append(QOS_LIVELINESS, q.livelinessMs);
    append(QOS_TOPIC, q.topicKind);
    return out;
}

void parseQosBytes(const QByteArray& bytes, QoSProfile* q)
{
    if (!q) return;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    while (!ds.atEnd()) {
        quint16 id = 0, len = 0;
        ds >> id >> len;
        if (ds.status() != QDataStream::Ok || len == 0 || ds.device()->bytesAvailable() < len) return;
        QByteArray value(len, char(0));
        if (ds.readRawData(value.data(), len) != len) return;
        QDataStream vs(value);
        vs.setByteOrder(QDataStream::LittleEndian);
        quint32 u = 0;
        if (len >= 4) vs >> u;
        switch (id) {
        case QOS_RELIABILITY: q->reliability = u; break;
        case QOS_HISTORY: q->historyDepth = u; break;
        case QOS_MAX_AGE: q->maxAgeMs = u; break;
        case QOS_DEADLINE: q->deadlineMs = u; break;
        case QOS_LIVELINESS: q->livelinessMs = u; break;
        case QOS_TOPIC: q->topicKind = u; break;
        default: break;
        }
    }
}

bool readRtpsHeader(QDataStream& ds, QByteArray* guidPrefix)
{
    char p[4] = { 0 };
    if (ds.readRawData(p, 4) != 4) return false;
    if (p[0] != 'R' || p[1] != 'T' || p[2] != 'P' || p[3] != 'S') return false;
    quint8 a = 0, b = 0, c = 0, d = 0;
    ds >> a >> b >> c >> d;
    Q_UNUSED(a)
    Q_UNUSED(b)
    Q_UNUSED(c)
    Q_UNUSED(d)
    QByteArray gp(12, char(0));
    if (ds.readRawData(gp.data(), 12) != 12) return false;
    if (guidPrefix) *guidPrefix = gp;
    return true;
}
}

QByteArray makeGuidPrefix(quint32 domainId, quint16 nodeRole, const QByteArray& seed6)
{
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << domainId << nodeRole;
    QByteArray seed = seed6.left(6);
    if (seed.size() < 6) seed.append(QByteArray(6 - seed.size(), char(0)));
    out.append(seed);
    if (out.size() < 12) out.append(QByteArray(12 - out.size(), char(0)));
    return out.left(12);
}


QByteArray makeGuidPrefixFromFcName(quint16 domainId, quint16 roleId, const QString& fcName, quint16 instanceId)
{
    QByteArray out;
    out.reserve(12);
    auto appendU16 = [&](quint16 v) {
        out.append(char((v >> 8) & 0xff));
        out.append(char(v & 0xff));
    };
    appendU16(domainId);
    appendU16(roleId);
    QByteArray hash = QCryptographicHash::hash(fcName.toUtf8(), QCryptographicHash::Sha1);
    out.append(hash.left(6));
    appendU16(instanceId);
    if (out.size() < 12) out.append(QByteArray(12 - out.size(), char(0)));
    return out.left(12);
}

QByteArray serializeData(const DataMessage& m)
{
    QByteArray qb = qosBytes(m.qos);
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    writeRtpsHeader(ds, m.guidPrefix);
    writeInfoTs(ds, m.timestampMs);
    quint16 body = quint16(2 + 2 + 4 + 4 + 8 + 4 + 4 + qb.size() + m.payload.size());
    quint8 flags = 3;
    if (m.qos.reliability == quint32(ReliabilityKind::Reliable)) flags |= 4;
    writeSubHeader(ds, quint8(SubmessageId::Data), flags, body);
    ds << quint16(0) << quint16(0)
       << m.readerId << m.writerId << m.writerSeqNum
       << quint32(qb.size()) << quint32(m.payload.size());
    if (!qb.isEmpty()) ds.writeRawData(qb.constData(), qb.size());
    if (!m.payload.isEmpty()) ds.writeRawData(m.payload.constData(), m.payload.size());
    return out;
}

QByteArray serializeHeartbeat(const HeartbeatMessage& m)
{
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    writeRtpsHeader(ds, m.guidPrefix);
    writeInfoTs(ds, m.timestampMs);
    writeSubHeader(ds, quint8(SubmessageId::Heartbeat), 1, 24);
    ds << m.writerId << m.firstSeqNum << m.lastSeqNum << m.count;
    return out;
}

QByteArray serializeAckNack(const AckNackMessage& m)
{
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    writeRtpsHeader(ds, m.guidPrefix);
    writeInfoTs(ds, m.timestampMs);
    quint16 body = quint16(4 + 4 + 4 + m.requestedSeqNums.size() * 8 + 4);
    writeSubHeader(ds, quint8(SubmessageId::AckNack), 1, body);
    ds << m.readerId << m.writerId << quint32(m.requestedSeqNums.size());
    for (auto seq : m.requestedSeqNums) ds << seq;
    ds << m.count;
    return out;
}

bool parseMessage(const QByteArray& dg, ParsedMessage* out)
{
    if (!out) return false;
    *out = ParsedMessage{};
    QDataStream ds(dg);
    ds.setByteOrder(QDataStream::LittleEndian);
    QByteArray gp;
    if (!readRtpsHeader(ds, &gp)) return false;
    qint64 ts = 0;
    while (!ds.atEnd()) {
        quint8 id = 0, flags = 0;
        quint16 bodyLen = 0;
        ds >> id >> flags >> bodyLen;
        Q_UNUSED(flags)
        if (ds.status() != QDataStream::Ok) return false;
        qint64 start = ds.device()->pos();
        if (id == quint8(SubmessageId::InfoTs)) {
            qint32 seconds = 0;
            quint32 nanoseconds = 0;
            ds >> seconds >> nanoseconds;
            ts = qint64(seconds) * 1000 + qint64(nanoseconds / 1000000U);
        } else if (id == quint8(SubmessageId::Data)) {
            DataMessage m;
            m.guidPrefix = gp;
            m.timestampMs = ts;
            quint16 extraFlags = 0, octetsToInlineQos = 0;
            quint32 qosLen = 0, payloadLen = 0;
            ds >> extraFlags >> octetsToInlineQos >> m.readerId >> m.writerId >> m.writerSeqNum >> qosLen >> payloadLen;
            Q_UNUSED(extraFlags)
            Q_UNUSED(octetsToInlineQos)
            if (ds.status() != QDataStream::Ok) return false;
            QByteArray qb(int(qosLen), char(0));
            if (qosLen && ds.readRawData(qb.data(), int(qosLen)) != int(qosLen)) return false;
            parseQosBytes(qb, &m.qos);
            m.payload.resize(int(payloadLen));
            if (payloadLen && ds.readRawData(m.payload.data(), int(payloadLen)) != int(payloadLen)) return false;
            out->kind = ParsedKind::Data;
            out->data = m;
            return true;
        } else if (id == quint8(SubmessageId::Heartbeat)) {
            HeartbeatMessage h;
            h.guidPrefix = gp;
            h.timestampMs = ts;
            ds >> h.writerId >> h.firstSeqNum >> h.lastSeqNum >> h.count;
            if (ds.status() != QDataStream::Ok) return false;
            out->kind = ParsedKind::Heartbeat;
            out->heartbeat = h;
            return true;
        } else if (id == quint8(SubmessageId::AckNack)) {
            AckNackMessage a;
            a.guidPrefix = gp;
            a.timestampMs = ts;
            quint32 count = 0;
            ds >> a.readerId >> a.writerId >> count;
            for (quint32 i = 0; i < count; ++i) { quint64 seq = 0; ds >> seq; a.requestedSeqNums.append(seq); }
            ds >> a.count;
            if (ds.status() != QDataStream::Ok) return false;
            out->kind = ParsedKind::AckNack;
            out->acknack = a;
            return true;
        } else {
            ds.skipRawData(int(bodyLen));
        }
        qint64 used = ds.device()->pos() - start;
        if (used < bodyLen) ds.skipRawData(int(bodyLen - used));
    }
    return false;
}

QString reliabilityToString(quint32 v) { return v == 1 ? "reliable" : "best_effort"; }
QString topicToString(quint32 v) { return v == 1 ? "snapshot" : (v == 2 ? "delta" : "unknown"); }

} // namespace rtpslite
