#ifndef SETTINGSTAB_H
#define SETTINGSTAB_H

#include <QWidget>

QT_BEGIN_NAMESPACE
class QComboBox;
class QStackedWidget;
QT_END_NAMESPACE

// "Настройки" tab: per-module-type configuration (channels, filters, etc.).
// Placeholder scaffold for now — concrete register maps for 12DI/12DO/4RTD/
// 4AIU/4AIC/4AO will be wired in once the specs are available.
class SettingsTab : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsTab(QWidget *parent = nullptr);

private:
    QWidget *makePlaceholderPage(const QString &moduleType);

    QComboBox      *m_moduleType;
    QStackedWidget *m_pages;
};

#endif // SETTINGSTAB_H
