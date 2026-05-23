#pragma once

#include <QObject>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QTimer>
#include <QUdpSocket>
#include <pcap/pcap.h>

#include "../shared/fcdiscovery.h"
#include "../shared/rtpslite.h"

class SwitchAgent : public QObject
{
    Q_OBJECT
public:
    SwitchAgent(const QString& bridge,
                const QString& switchId,
                const QHostAddress& centerHost,
                quint16 centerPort,
                const QHostAddress& rtpsLocalHost = QHostAddress(),
                QObject* parent = nullptr);
    ~SwitchAgent() override;
    bool start();

private slots:
    void txDiscovery();
    void rxDiscovery();
    void expireNeighbors();
    void sendPeriodicSnapshot();
    void sendBootstrapSnapshot();
    void sendHeartbeat();
    void sendPendingDelta();
    void scanEndpointPorts();
    void scanFcPorts();
    void processControlDatagrams();

private:
    struct FcPortState {
        QString portName;
        QString carrierAddress;
        QString wwpn;
        QString fcIdText;
        QString role = "isl";
        quint32 fcId = 0;
        pcap_t* pcap = nullptr;
        bool operUp = true;
        int upSamples = 0;
        int downSamples = 0;
        quint32 discoverySeq = 1;
    };

    struct Neighbor {
        QString localPort;
        QString localWwpn;
        QString localFcIdText;
        QString remoteSwitchId;
        QString remoteWwnn;
        QString remoteWwpn;
        QString remoteFcIdText;
        QString remotePort;
        qint64 expiresAt = 0;
        int missedCount = 0;
    };

    struct EndpointPortState {
        QString portName;
        QString endpointId;
        bool adminUp = false;
        bool operUp = false;
        qint64 lastSeenMs = 0;
    };

    struct WriterState {
        quint32 readerId = 0;
        quint32 writerId = 0;
        quint64 nextSeqNum = 1;
        rtpslite::QoSProfile qos;
        QHash<quint64, QByteArray> history;
        QList<quint64> historyOrder;
        QHash<quint64, int> retransmitCount;
    };

    QString runCmd(const QString& prog, const QStringList& args) const;
    QString getExternalId(const QString& ifName, const QString& key) const;
    QString getRole(const QString& ifName) const;
    bool isFcPortRole(const QString& role) const;
    bool isEndpointRole(const QString& role) const;
    QString deriveEndpointId(const QString& portName) const;
    bool readOperUp(const QString& ifName) const;
    bool readAdminUp(const QString& ifName) const;
    qint64 nowMs() const;
    int switchIndex() const;
    QString expectedRemotePortName(const QString& localPort) const;

    bool discoverFcPorts();
    bool discoverEndpointPorts();
    QJsonArray refreshEndpointPortStates(bool emitEvents);
    void initWriters();
    QJsonObject buildSnapshotObject() const;
    void appendDeltaEvent(const QJsonObject& event);
    void requestDeltaSoon();
    void sendSnapshot();
    void sendDelta(const QJsonArray& events);
    QByteArray sendData(WriterState& writer, const QJsonObject& obj);
    void storeHistory(WriterState& writer, quint64 seq, const QByteArray& datagram);
    WriterState* writerById(quint32 writerId);
    void handleAckNack(const rtpslite::AckNackMessage& ack);
    void upsertNeighborFromDiscovery(const QString& ingressPort, const fcdisc::DiscoveryMessage& parsed);

    QString m_bridge;
    QString m_switchId;
    QString m_wwnn;
    QHostAddress m_centerHost;
    quint16 m_centerPort = 0;
    QHostAddress m_rtpsLocalHost;
    QByteArray m_guidPrefix;

    QHash<QString, FcPortState> m_fcPorts;
    QHash<QString, Neighbor> m_neighbors;
    QHash<QString, EndpointPortState> m_endpointPorts;
    WriterState m_snapshotWriter;
    WriterState m_deltaWriter;
    QJsonArray m_pendingDeltaEvents;
    QUdpSocket m_udp;

    QTimer m_txTimer;
    QTimer m_rxTimer;
    QTimer m_expireTimer;
    QTimer m_snapshotTimer;
    QTimer m_bootstrapTimer;
    QTimer m_heartbeatTimer;
    QTimer m_deltaDebounceTimer;
    QTimer m_endpointScanTimer;
    QTimer m_fcPortScanTimer;

    int m_bootstrapRoundsRemaining = 0;
    quint32 m_heartbeatCount = 1;
};
