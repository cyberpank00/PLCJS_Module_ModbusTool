#include <QApplication>
#include <QIcon>
#include <QMetaType>

#include "MainWindow.h"
#include "tabs/FwWorker.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PLCJS Module Tool"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icon.ico")));

    // Required for queued signals/slots across the worker thread boundary.
    qRegisterMetaType<FwUpdateParams>("FwUpdateParams");
    qRegisterMetaType<boot::Status>("boot::Status");

    MainWindow w;
    w.show();
    return app.exec();
}
