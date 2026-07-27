#include "SettingsTab.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {
const QStringList kModuleTypes = {
    QStringLiteral("12DI"), QStringLiteral("12DO"), QStringLiteral("4RTD"),
    QStringLiteral("4AIU"), QStringLiteral("4AIC"), QStringLiteral("4AO"),
};
}

SettingsTab::SettingsTab(QWidget *parent)
    : QWidget(parent)
{
    m_moduleType = new QComboBox;
    m_moduleType->addItems(kModuleTypes);

    auto *typeRow = new QHBoxLayout;
    typeRow->addWidget(new QLabel(QStringLiteral("Тип модуля:")));
    typeRow->addWidget(m_moduleType);
    typeRow->addStretch();

    m_pages = new QStackedWidget;
    for (const QString &type : kModuleTypes)
        m_pages->addWidget(makePlaceholderPage(type));

    connect(m_moduleType, &QComboBox::currentIndexChanged,
            m_pages, &QStackedWidget::setCurrentIndex);

    auto *note = new QLabel(QStringLiteral(
        "Заглушка: карты регистров модулей будут добавлены после получения "
        "спецификаций. Поля ниже пока неактивны."));
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color:#888;"));

    auto *root = new QVBoxLayout(this);
    root->addLayout(typeRow);
    root->addWidget(m_pages, 1);
    root->addWidget(note);
}

QWidget *SettingsTab::makePlaceholderPage(const QString &moduleType)
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    auto *channels = new QGroupBox(QStringLiteral("Каналы (%1)").arg(moduleType));
    auto *chForm = new QFormLayout(channels);
    for (int i = 0; i < 4; ++i) {
        auto *sb = new QSpinBox;
        sb->setRange(0, 65535);
        sb->setEnabled(false);
        chForm->addRow(QStringLiteral("Канал %1 (TODO):").arg(i), sb);
    }

    auto *filters = new QGroupBox(QStringLiteral("Фильтры"));
    auto *fForm = new QFormLayout(filters);
    auto *filt = new QSpinBox;
    filt->setEnabled(false);
    fForm->addRow(QStringLiteral("Постоянная фильтра (TODO):"), filt);

    auto *misc = new QGroupBox(QStringLiteral("Прочее"));
    auto *mForm = new QFormLayout(misc);
    auto *addr = new QSpinBox;
    addr->setRange(1, 247);
    addr->setEnabled(false);
    mForm->addRow(QStringLiteral("Modbus адрес (TODO):"), addr);

    auto *btnRow = new QHBoxLayout;
    auto *readBtn = new QPushButton(QStringLiteral("Читать"));
    auto *writeBtn = new QPushButton(QStringLiteral("Записать"));
    readBtn->setEnabled(false);
    writeBtn->setEnabled(false);
    btnRow->addWidget(readBtn);
    btnRow->addWidget(writeBtn);
    btnRow->addStretch();

    layout->addWidget(channels);
    layout->addWidget(filters);
    layout->addWidget(misc);
    layout->addLayout(btnRow);
    layout->addStretch();
    return page;
}
