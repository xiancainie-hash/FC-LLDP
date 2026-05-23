#pragma once

#include <QHash>
#include <QHostAddress>
#include <QMainWindow>
#include <QSet>
#include <QStringList>
#include <QVector>

#include "topologylinkitem.h"
#include "../shared/rtpslite.h"

class QUdpSocket;
class QGraphicsScene;
class QGraphicsView;
class QLabel;
class QPlainTextEdit;
class QTableWidget;
class QTimer;
class TopologyNodeItem;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QHostAddress& listenHost, quint16 port, QWidget* parent = nullptr);

private slots:
    void processDatagrams();
    void maintenanceTick();
    void renderGraphIfDirty();
    void renderTableIfDirty();

private:
    struct FcNeighborInfo {
        QString localPort;
        QString localWwpn;
        QString localFcId;
        QString remoteSwitchId;
        QString remoteWwnn;
        QString remotePort;
        QString remoteWwpn;
        QString remoteFcId;
    };

    struct FcPortInfo {
        QString portName;
        QString wwpn;
        QString fcId;
        QString role;
        bool operUp = true;
        qint64 lastChangeMs = 0;
    };

    struct EndpointPortInfo {
        QString portName;
        QString endpointId;
        QString role;
        bool adminUp = false;
        bool operUp = false;
        qint64 lastSeenMs = 0;
    };

    struct Snapshot {
        QString fcSwitchId;
        QString ovsNode;
        QString wwnn;
        QVector<FcPortInfo> fcPorts;
        QVector<FcNeighborInfo> fcNeighbors;
        QVector<EndpointPortInfo> endpointPorts;
        qint64 ts = 0;
        qint64 recvMs = 0;
        quint32 deadlineMs = 3000;
        quint32 livelinessMs = 5000;
    };

    struct LinkInfo {
        QString aSw;
        QString aPort;
        QString bSw;
        QString bPort;
        bool ab = false;
        bool ba = false;
    };

    struct StableLinkState {
        QString aSw;
        QString aPort;
        QString bSw;
        QString bPort;
        qint64 lastSeenMs = 0;
        qint64 lastConfirmedMs = 0;
        TopologyLinkStatus displayStatus = TopologyLinkStatus::Down;
    };

    struct WriterRecvState {
        quint64 expectedSeq = 0;
        QSet<quint64> received;
        QSet<quint64> missing;
        qint64 lastAckNackMs = 0;
        QHostAddress lastAddr;
        quint16 lastPort = 0;
        quint32 readerId = 0;
    };

    void buildUi();
    void appendLog(const QString& text);
    void ensureFixedNodes();
    void applySnapshotPayload(const QJsonObject& obj, const rtpslite::QoSProfile* qos);
    void applyDeltaPayload(const QJsonObject& obj, const rtpslite::QoSProfile* qos);
    void trackDataSeq(const rtpslite::DataMessage& data, const QHostAddress& sender, quint16 port);
    void handleHeartbeat(const rtpslite::HeartbeatMessage& heartbeat, const QHostAddress& sender, quint16 port);
    void sendAckNack(quint32 writerId);
    void upsertNeighbor(Snapshot& snap, const FcNeighborInfo& info);
    void removeNeighbor(Snapshot& snap, const QString& localPort);
    void upsertEndpointPort(Snapshot& snap, const EndpointPortInfo& info);
    void upsertFcPort(Snapshot& snap, const FcPortInfo& info);
    void rebuildModel();
    void updateSwitchNodes(const QStringList& online);
    void updateEndpointNodes(const QHash<QString, bool>& endpointMap);
    void updateLinks(const QHash<QString, bool>& endpointMap);
    void updateStatusText();
    QString makeLinkKey(const QString& aSw, const QString& aPort, const QString& bSw, const QString& bPort) const;
    bool isFresh(qint64 timestampMs, quint32 maxAgeMs) const;
    bool isEndpointPortDown(const QString& sw, const QString& port) const;
    QString buildGraphSignature() const;
    QString buildTableSignature() const;

    QUdpSocket* m_udp = nullptr;
    QTimer* m_maintenanceTimer = nullptr;
    QTimer* m_graphTimer = nullptr;
    QTimer* m_tableTimer = nullptr;
    QGraphicsScene* m_scene = nullptr;
    QGraphicsView* m_view = nullptr;
    QLabel* m_status = nullptr;
    QPlainTextEdit* m_log = nullptr;
    QTableWidget* m_table = nullptr;

    QHash<QString, Snapshot> m_snaps;
    QHash<QString, QString> m_wwnnToSwitch;
    QHash<QString, TopologyNodeItem*> m_nodeItems;
    QHash<QString, TopologyLinkItem*> m_linkItems;
    QHash<QString, StableLinkState> m_fcIslLinkStates;
    QHash<quint32, WriterRecvState> m_writerRecv;

    QStringList m_cachedOnlineSwitches;
    QHash<QString, bool> m_cachedEndpointOnlineMap;
    int m_cachedVisibleIslLinks = 0;
    QString m_lastGraphSignature;
    QString m_lastTableSignature;
    bool m_modelDirty = true;
    bool m_graphDirty = true;
    bool m_tableDirty = true;
};
