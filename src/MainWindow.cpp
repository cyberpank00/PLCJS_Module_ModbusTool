#include "MainWindow.h"

#include <QTabWidget>

#include "tabs/FwUpdateTab.h"
#include "tabs/OnlineTab.h"
#include "tabs/SettingsTab.h"
#include "version.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("PLCJS Module Tool ") + MODULE_TOOL_VERSION_QSTR);

    auto *tabs = new QTabWidget;
    tabs->addTab(new OnlineTab, QStringLiteral("Онлайн"));
    tabs->addTab(new SettingsTab, QStringLiteral("Настройки"));
    tabs->addTab(new FwUpdateTab, QStringLiteral("Обновление FW"));

    setCentralWidget(tabs);
    resize(900, 600);
}
