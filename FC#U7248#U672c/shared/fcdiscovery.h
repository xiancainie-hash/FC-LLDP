#pragma once

#include <QByteArray>
#include <QString>
#include <optional>

namespace fcdisc {

// OVS/veth 实验承载层协议类型。注意：该字段只用于 OVS 实验转发与抓包识别，
// 不作为 FC 拓扑语义字段。
constexpr quint16 CARRIER_PROTO_FC_DISCOVERY = 0x88B5;

// 简化 FC 帧头中的发现帧类型。本文用 TYPE=0xF0 表示实验性 FC Discovery。
constexpr quint8 FC_TYPE_DISCOVERY = 0xF0;
constexpr quint8 FC_R_CTL_DISCOVERY = 0x22;

// FC Discovery TLV 类型。Payload 不再有 Magic/Version 等固定头，直接由 TLV 序列组成。
enum class TlvType : quint8 {
    End = 0x00,
    SwitchId = 0x01,
    PortId = 0x02,
    Wwnn = 0x03,
    Wwpn = 0x04,
    FcId = 0x05,
    HoldTime = 0x06,
    Capability = 0x07,
    PortRole = 0x08,
    SystemDescription = 0x09,
    LinkMetric = 0x0A,
    Timestamp = 0x0B,
    SequenceExtension = 0x0C
};

struct DiscoveryMessage
{
    QString switchId;      // FC switch logical ID, e.g. s1
    QString localPort;     // FC port logical name, e.g. s1-s2
    QString wwnn;          // simulated node WWN
    QString wwpn;          // simulated port WWN
    QString portRole;      // isl / endpoint
    QString capability;    // e.g. fc_switch
    QString systemDesc;    // human readable description

    quint32 srcFcId = 0;        // 24-bit FC source ID encoded in low 24 bits
    quint32 dstFcId = 0xFFFFFF; // broadcast / unknown destination for discovery
    quint32 seqNum = 0;         // full discovery sequence, SEQ_CNT uses low 16 bits
    quint16 holdTimeSec = 4;
};

QByteArray macStringToBytes(const QString& mac);
QString macBytesToString(const QByteArray& mac);

// 构造：OVS Experimental Carrier Header + Simplified FC Frame Header + FC Discovery TLV Sequence.
QByteArray buildFcDiscoveryFrame(const QByteArray& srcCarrierAddress,
                                 const DiscoveryMessage& msg);

// 解析：检查承载层协议类型与 FC TYPE，再解析 TLV 序列。
std::optional<DiscoveryMessage> parseFcDiscoveryFrame(const uint8_t* data, int len);
std::optional<DiscoveryMessage> parseFcDiscoveryFrame(const QByteArray& frame);

QString makeWwnn(const QString& switchId);
QString makeWwpn(const QString& switchId, const QString& portName);
quint32 makeFcId(const QString& switchId, const QString& portName);
QString fcIdToString(quint32 fcId);

} // namespace fcdisc
