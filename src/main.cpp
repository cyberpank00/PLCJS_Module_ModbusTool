#include <QApplication>
#include <QIcon>
#include <QMetaType>
#include <QNetworkProxy>

#include "MainWindow.h"
#include "tabs/FwWorker.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PLCJS Module Tool"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icon.ico")));

    // The tool only ever talks to modules on the local segment. Never pick up
    // a system proxy (VPN/TUN clients install one): with it, QTcpSocket fails
    // with "proxy type is invalid" and QUdpSocket tries SOCKS5 UDP ASSOCIATE.
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);

    // Required for queued signals/slots across the worker thread boundary.
    qRegisterMetaType<FwUpdateParams>("FwUpdateParams");
    qRegisterMetaType<boot::Status>("boot::Status");

    MainWindow w;
    w.show();
    return app.exec();
}
