#include "fcdiscovery.h"

#include <QCryptographicHash>
#include <QStringList>

namespace fcdisc {
namespace {

void appendU8(QByteArray& out, quint8 v) { out.append(char(v)); }
void appendU16(QByteArray& out, quint16 v)
{
    out.append(char((v >> 8) & 0xff));
    out.append(char(v & 0xff));
}
void appendU24(QByteArray& out, quint32 v)
{
    out.append(char((v >> 16) & 0xff));
    out.append(char((v >> 8) & 0xff));
    out.append(char(v & 0xff));
}
void appendU32(QByteArray& out, quint32 v)
{
    out.append(char((v >> 24) & 0xff));
    out.append(char((v >> 16) & 0xff));
    out.append(char((v >> 8) & 0xff));
    out.append(char(v & 0xff));
}

quint16 readU16(const uint8_t* p)
{
    return (quint16(p[0]) << 8) | quint16(p[1]);
}
quint32 readU24(const uint8_t* p)
{
    return (quint32(p[0]) << 16) | (quint32(p[1]) << 8) | quint32(p[2]);
}
quint32 readU32(const uint8_t* p)
{
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
}

int numericSuffix(const QString& s)
{
    QString digits;
    for (QChar c : s) {
        if (c.isDigit()) digits.append(c);
    }
    bool ok = false;
    int v = digits.toInt(&ok);
    return ok ? v : 0;
}

int remoteIndexFromPort(const QString& switchId, const QString& portName)
{
    // 常见端口名：s1-s2、s2-s1、s3-s5。优先取非本端的节点编号作为端口编号。
    const int self = numericSuffix(switchId);
    const QStringList parts = portName.split('-', Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        int n = numericSuffix(part);
        if (n > 0 && n != self) return n;
    }

    QByteArray h = QCryptographicHash::hash(portName.toUtf8(), QCryptographicHash::Sha1);
    return (quint8(h[0]) % 250) + 1;
}

QString wwnString(quint8 prefix, int a, int b)
{
    // 8 字节可读 WWN。prefix=0x20 表示 WWNN，prefix=0x21 表示 WWPN。
    // 该规则用于实验环境静态生成，不声称复现真实厂商 OUI 分配。
    return QString("%1:00:00:00:00:%2:00:%3")
        .arg(prefix, 2, 16, QLatin1Char('0'))
        .arg(a & 0xff, 2, 16, QLatin1Char('0'))
        .arg(b & 0xff, 2, 16, QLatin1Char('0'))
        .toLower();
}

void appendTlv(QByteArray& out, TlvType type, const QByteArray& value)
{
    appendU8(out, quint8(type));
    appendU16(out, quint16(value.size()));
    out.append(value);
}

void appendStringTlv(QByteArray& out, TlvType type, const QString& value)
{
    appendTlv(out, type, value.toUtf8());
}

QByteArray buildPayload(const DiscoveryMessage& msg, quint16* tlvCount)
{
    QByteArray payload;
    quint16 count = 0;
    auto add = [&](TlvType type, const QByteArray& value) {
        appendTlv(payload, type, value);
        count++;
    };
    auto addText = [&](TlvType type, const QString& text) {
        add(type, text.toUtf8());
    };

    addText(TlvType::SwitchId, msg.switchId);
    addText(TlvType::PortId, msg.localPort);
    addText(TlvType::Wwnn, msg.wwnn);
    addText(TlvType::Wwpn, msg.wwpn);

    QByteArray fcId;
    appendU24(fcId, msg.srcFcId & 0xFFFFFF);
    add(TlvType::FcId, fcId);

    QByteArray hold;
    appendU16(hold, msg.holdTimeSec);
    add(TlvType::HoldTime, hold);

    if (!msg.capability.isEmpty()) addText(TlvType::Capability, msg.capability);
    if (!msg.portRole.isEmpty()) addText(TlvType::PortRole, msg.portRole);
    if (!msg.systemDesc.isEmpty()) addText(TlvType::SystemDescription, msg.systemDesc);

    // 可选完整 32 位序号。FC Header.SEQ_CNT 仅保留低 16 位，扩展 TLV 便于调试和丢包分析。
    QByteArray seq;
    appendU32(seq, msg.seqNum);
    add(TlvType::SequenceExtension, seq);

    add(TlvType::End, QByteArray());

    if (tlvCount) *tlvCount = count;
    return payload;
}

void appendCarrierHeader(QByteArray& out, const QByteArray& src)
{
    // 实验组播地址。只用于 OVS/veth 承载，不进入拓扑语义。
    const uchar dst[6] = {0x01, 0x10, 0x18, 0x01, 0x00, 0x02};
    for (uchar b : dst) out.append(char(b));

    QByteArray s = src.left(6);
    if (s.size() < 6) s.append(QByteArray(6 - s.size(), char(0)));
    out.append(s);
    appendU16(out, CARRIER_PROTO_FC_DISCOVERY);
}

void appendFcHeader(QByteArray& out, const DiscoveryMessage& msg, quint16 payloadLen, quint16 tlvCount)
{
    appendU8(out, FC_R_CTL_DISCOVERY);
    appendU24(out, msg.dstFcId & 0xFFFFFF);
    appendU8(out, 0x00); // CS_CTL / priority
    appendU24(out, msg.srcFcId & 0xFFFFFF);
    appendU8(out, FC_TYPE_DISCOVERY);
    appendU24(out, 0x000000); // F_CTL
    appendU8(out, 0x00);     // SEQ_ID
    appendU8(out, 0x00);     // DF_CTL
    appendU16(out, quint16(msg.seqNum & 0xffff));
    appendU16(out, 0xffff);  // OX_ID
    appendU16(out, 0xffff);  // RX_ID

    // Parameter 高 16 位为 Payload Length，低 16 位为 TLV Count。
    quint32 parameter = (quint32(payloadLen) << 16) | quint32(tlvCount);
    appendU32(out, parameter);
}

QString tlvText(const uint8_t* p, quint16 len)
{
    return QString::fromUtf8(reinterpret_cast<const char*>(p), int(len));
}

} // namespace

QByteArray macStringToBytes(const QString& mac)
{
    QByteArray out;
    const QStringList parts = mac.split(':');
    for (const QString& p : parts) {
        bool ok = false;
        int v = p.toInt(&ok, 16);
        if (!ok) return QByteArray();
        out.append(char(v & 0xff));
    }
    return out;
}

QString macBytesToString(const QByteArray& mac)
{
    QStringList parts;
    for (unsigned char c : mac) {
        parts << QString("%1").arg(int(c), 2, 16, QLatin1Char('0'));
    }
    return parts.join(':');
}

QString makeWwnn(const QString& switchId)
{
    const int idx = numericSuffix(switchId);
    return wwnString(0x20, 0, idx);
}

QString makeWwpn(const QString& switchId, const QString& portName)
{
    const int sw = numericSuffix(switchId);
    const int port = remoteIndexFromPort(switchId, portName);
    return wwnString(0x21, sw, port);
}

quint32 makeFcId(const QString& switchId, const QString& portName)
{
    const int sw = numericSuffix(switchId) & 0xff;
    const int port = remoteIndexFromPort(switchId, portName) & 0xff;
    return (quint32(sw) << 16) | quint32(port);
}

QString fcIdToString(quint32 fcId)
{
    return QString("0x%1").arg(fcId & 0xFFFFFF, 6, 16, QLatin1Char('0')).toLower();
}

QByteArray buildFcDiscoveryFrame(const QByteArray& srcCarrierAddress, const DiscoveryMessage& msg)
{
    quint16 tlvCount = 0;
    QByteArray payload = buildPayload(msg, &tlvCount);

    QByteArray out;
    appendCarrierHeader(out, srcCarrierAddress);
    appendFcHeader(out, msg, quint16(payload.size()), tlvCount);
    out.append(payload);
    return out;
}

std::optional<DiscoveryMessage> parseFcDiscoveryFrame(const QByteArray& frame)
{
    return parseFcDiscoveryFrame(reinterpret_cast<const uint8_t*>(frame.constData()), frame.size());
}

std::optional<DiscoveryMessage> parseFcDiscoveryFrame(const uint8_t* data, int len)
{
    if (!data || len < 14 + 24) return std::nullopt;
    if (readU16(data + 12) != CARRIER_PROTO_FC_DISCOVERY) return std::nullopt;

    const uint8_t* fc = data + 14;
    const quint8 rCtl = fc[0];
    const quint32 dId = readU24(fc + 1);
    const quint32 sId = readU24(fc + 5);
    const quint8 type = fc[8];
    const quint16 seqCnt = readU16(fc + 14);
    const quint32 parameter = readU32(fc + 20);

    Q_UNUSED(rCtl)
    Q_UNUSED(dId)

    if (type != FC_TYPE_DISCOVERY) return std::nullopt;

    const quint16 payloadLen = quint16((parameter >> 16) & 0xffff);
    const quint16 tlvCount = quint16(parameter & 0xffff);
    if (payloadLen == 0 || 14 + 24 + payloadLen > len) return std::nullopt;

    const uint8_t* p = data + 14 + 24;
    const uint8_t* end = p + payloadLen;

    DiscoveryMessage msg;
    msg.srcFcId = sId;
    msg.dstFcId = dId;
    msg.seqNum = seqCnt;

    quint16 parsed = 0;
    bool sawEnd = false;
    while (p + 3 <= end && parsed < tlvCount) {
        TlvType t = TlvType(p[0]);
        quint16 l = readU16(p + 1);
        p += 3;
        if (p + l > end) return std::nullopt;

        switch (t) {
        case TlvType::End:
            sawEnd = true;
            break;
        case TlvType::SwitchId:
            msg.switchId = tlvText(p, l);
            break;
        case TlvType::PortId:
            msg.localPort = tlvText(p, l);
            break;
        case TlvType::Wwnn:
            msg.wwnn = tlvText(p, l);
            break;
        case TlvType::Wwpn:
            msg.wwpn = tlvText(p, l);
            break;
        case TlvType::FcId:
            if (l == 3) msg.srcFcId = readU24(p);
            break;
        case TlvType::HoldTime:
            if (l >= 2) msg.holdTimeSec = readU16(p);
            break;
        case TlvType::Capability:
            msg.capability = tlvText(p, l);
            break;
        case TlvType::PortRole:
            msg.portRole = tlvText(p, l);
            break;
        case TlvType::SystemDescription:
            msg.systemDesc = tlvText(p, l);
            break;
        case TlvType::SequenceExtension:
            if (l >= 4) msg.seqNum = readU32(p);
            break;
        default:
            break;
        }

        p += l;
        parsed++;
        if (sawEnd) break;
    }

    if (!sawEnd) return std::nullopt;
    if (parsed != tlvCount) return std::nullopt;
    if (msg.switchId.isEmpty() || msg.localPort.isEmpty() || msg.wwnn.isEmpty() || msg.wwpn.isEmpty()) {
        return std::nullopt;
    }
    return msg;
}

} // namespace fcdisc
