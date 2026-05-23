#include "switchagent.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QHostAddress>
#include <QDebug>

static bool parseHostPort(const QString& s, QHostAddress& host, quint16& port)
{
    const auto parts = s.split(':');
    if (parts.size() != 2) return false;
    host = QHostAddress(parts[0]);
    bool ok = false;
    port = parts[1].toUShort(&ok);
    return !host.isNull() && ok;
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();

    QCommandLineOption brOpt("bridge", "OVS bridge / simulated FC switch name", "bridge");
    QCommandLineOption idOpt("switch-id", "FC switch id", "id");
    QCommandLineOption centerOpt("center", "center host:port", "addr", "127.0.0.1:5000");
    QCommandLineOption rtpsLocalIpOpt("rtps-local-ip",
                                      "local source IP used by RTPS-Lite UDP socket",
                                      "ip",
                                      "");

    parser.addOption(brOpt);
    parser.addOption(idOpt);
    parser.addOption(centerOpt);
    parser.addOption(rtpsLocalIpOpt);
    parser.process(app);

    QHostAddress host;
    quint16 port = 0;
    if (!parseHostPort(parser.value(centerOpt), host, port)) {
        qCritical() << "invalid center address";
        return 1;
    }

    QString bridge = parser.value(brOpt);
    QString switchId = parser.value(idOpt);
    if (switchId.isEmpty()) switchId = bridge;

    QHostAddress rtpsLocalHost;
    const QString rtpsLocalIp = parser.value(rtpsLocalIpOpt).trimmed();
    if (!rtpsLocalIp.isEmpty()) {
        rtpsLocalHost = QHostAddress(rtpsLocalIp);
        if (rtpsLocalHost.isNull()) {
            qCritical() << "invalid rtps local ip" << rtpsLocalIp;
            return 1;
        }
    }

    qInfo() << "agent starting"
            << "bridge=" << bridge
            << "switch-id=" << switchId
            << "center=" << host.toString() << port
            << "rtps-local-ip=" << (rtpsLocalIp.isEmpty() ? QString("<auto>") : rtpsLocalIp);

    SwitchAgent agent(bridge, switchId, host, port, rtpsLocalHost);
    if (!agent.start()) return 2;
    return app.exec();
}
