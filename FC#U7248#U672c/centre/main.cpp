#include "mainwindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QHostAddress>
#include <QDebug>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption portOpt("port", "RTPS-Lite listen port", "port", "5000");
    QCommandLineOption listenOpt("listen", "RTPS-Lite listen IPv4 address", "addr", "0.0.0.0");
    parser.addOption(portOpt);
    parser.addOption(listenOpt);
    parser.process(app);

    bool ok = false;
    quint16 port = parser.value(portOpt).toUShort(&ok);
    if (!ok || port == 0) port = 5000;

    QHostAddress listenHost(parser.value(listenOpt));
    if (listenHost.isNull()) {
        qCritical() << "invalid listen address" << parser.value(listenOpt);
        return 1;
    }

    MainWindow w(listenHost, port);
    w.show();
    return app.exec();
}
