#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

namespace rtpslite {

enum class SubmessageId : quint8 {
    AckNack = 0x06,
    Heartbeat = 0x07,
    InfoTs = 0x09,
    Data = 0x15
};

enum class ReliabilityKind : quint32 {
    BestEffort = 0,
    Reliable = 1
};

enum class TopicKind : quint32 {
    Snapshot = 1,
    Delta = 2
};

struct QoSProfile {
    quint32 reliability = 0;
    quint32 historyDepth = 1;
    quint32 maxAgeMs = 5000;
    quint32 deadlineMs = 3000;
    quint32 livelinessMs = 5000;
    quint32 topicKind = 1;
};

struct DataMessage {
    QByteArray guidPrefix;
    quint32 readerId = 0;
    quint32 writerId = 0;
    quint64 writerSeqNum = 0;
    qint64 timestampMs = 0;
    QoSProfile qos;
    QByteArray payload;
};

struct HeartbeatMessage {
    QByteArray guidPrefix;
    quint32 writerId = 0;
    quint64 firstSeqNum = 0;
    quint64 lastSeqNum = 0;
    quint32 count = 0;
    qint64 timestampMs = 0;
};

struct AckNackMessage {
    QByteArray guidPrefix;
    quint32 readerId = 0;
    quint32 writerId = 0;
    QList<quint64> requestedSeqNums;
    quint32 count = 0;
    qint64 timestampMs = 0;
};

enum class ParsedKind {
    Invalid,
    Data,
    Heartbeat,
    AckNack
};

struct ParsedMessage {
    ParsedKind kind = ParsedKind::Invalid;
    DataMessage data;
    HeartbeatMessage heartbeat;
    AckNackMessage acknack;
};

constexpr quint32 READER_SNAPSHOT = 0x00001001;
constexpr quint32 READER_DELTA   = 0x00001002;

QByteArray makeGuidPrefix(quint32 domainId, quint16 nodeRole, const QByteArray& seed6);
QByteArray makeGuidPrefixFromFcName(quint16 domainId, quint16 roleId, const QString& fcName, quint16 instanceId = 1);
QByteArray serializeData(const DataMessage& msg);
QByteArray serializeHeartbeat(const HeartbeatMessage& msg);
QByteArray serializeAckNack(const AckNackMessage& msg);
bool parseMessage(const QByteArray& datagram, ParsedMessage* out);
QString reliabilityToString(quint32 v);
QString topicToString(quint32 v);

} // namespace rtpslite
