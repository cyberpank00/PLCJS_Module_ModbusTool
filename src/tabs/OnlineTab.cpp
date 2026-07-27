#include "OnlineTab.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <QtGlobal>
#include <cmath>
#include <cstring>

#include "modbus/ModbusTcpClient.h"
#include "protocol/BootloaderProtocol.h"

namespace {
// Combine two Modbus registers (HIGH word first) into a float32.
float regsToFloat(quint16 hi, quint16 lo)
{
    const quint32 bits = (quint32(hi) << 16) | quint32(lo);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

// Split a float32 into two Modbus registers (HIGH word first).
void floatToRegs(float f, quint16 &hi, quint16 &lo)
{
    quint32 bits;
    std::memcpy(&bits, &f, sizeof(bits));
    hi = quint16((bits >> 16) & 0xFFFF);
    lo = quint16(bits & 0xFFFF);
}
} // namespace

OnlineTab::OnlineTab(QWidget *parent)
    : QWidget(parent)
{
    // ---- Connection controls --------------------------------------------
    m_map = new QComboBox;
    m_map->addItems(maps::mapNames());

    m_ip = new QLineEdit(QStringLiteral("192.168.142.89"));
    m_port = new QSpinBox;
    m_port->setRange(1, 65535);
    m_port->setValue(boot::kDefaultPort);
    m_unitId = new QSpinBox;
    m_unitId->setRange(1, 247);
    m_unitId->setValue(boot::kDefaultUnitId);
    m_func = new QComboBox;
    m_func->addItem(QStringLiteral("Input (FC04)"));
    m_func->addItem(QStringLiteral("Holding (FC03)"));
    m_base = new QComboBox;
    m_base->addItem(QStringLiteral("dec"));
    m_base->addItem(QStringLiteral("hex"));

    auto *connBox = new QGroupBox(QStringLiteral("Подключение"));
    auto *connForm = new QFormLayout(connBox);
    connForm->addRow(QStringLiteral("Карта модуля:"), m_map);
    connForm->addRow(QStringLiteral("IP:"), m_ip);
    connForm->addRow(QStringLiteral("Порт:"), m_port);
    connForm->addRow(QStringLiteral("Unit ID:"), m_unitId);
    connForm->addRow(QStringLiteral("Регистры:"), m_func);
    connForm->addRow(QStringLiteral("Формат:"), m_base);

    // ---- Register table --------------------------------------------------
    m_table = new QTableWidget(0, ColCount);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("Имя"), QStringLiteral("Тип"), QStringLiteral("Адрес"),
         QStringLiteral("Значение (dec)"), QStringLiteral("Значение (hex)"),
         QStringLiteral("Расшифровка"), QStringLiteral("Запись")});
    m_table->verticalHeader()->setVisible(false);
    auto *hh = m_table->horizontalHeader();
    hh->setSectionResizeMode(ColName, QHeaderView::Stretch);
    hh->setSectionResizeMode(ColDecoded, QHeaderView::Stretch);
    hh->setSectionResizeMode(ColWrite, QHeaderView::Stretch);

    // ---- Buttons + status ------------------------------------------------
    auto *readBtn = new QPushButton(QStringLiteral("Читать"));
    auto *writeBtn = new QPushButton(QStringLiteral("Записать"));
    connect(readBtn, &QPushButton::clicked, this, &OnlineTab::onRead);
    connect(writeBtn, &QPushButton::clicked, this, &OnlineTab::onWrite);
    connect(m_map, &QComboBox::currentIndexChanged, this, &OnlineTab::onMapChanged);

    auto *btnRow = new QHBoxLayout;
    btnRow->addWidget(readBtn);
    btnRow->addWidget(writeBtn);
    btnRow->addStretch();

    m_status = new QLabel(QStringLiteral("Готово"));

    auto *right = new QVBoxLayout;
    right->addWidget(m_table);
    right->addLayout(btnRow);
    right->addWidget(m_status);

    auto *root = new QHBoxLayout(this);
    root->addWidget(connBox, 0);
    root->addLayout(right, 1);

    rebuildTable(); // start in free mode
}

// ---------------------------------------------------------------------------
// Table construction
// ---------------------------------------------------------------------------
void OnlineTab::rebuildTable()
{
    const maps::MapId id = maps::mapIdForIndex(m_map->currentIndex());

    m_rows.clear();
    m_table->clearContents();
    m_table->setRowCount(0);

    if (id == maps::MapId::Free) {
        m_func->setEnabled(true);
        buildFreeRows();
        setStatus(QStringLiteral("Свободная карта: задайте адреса вручную"));
        return;
    }

    // Named map: the per-row type replaces the global FC selector.
    m_func->setEnabled(false);

    const QVector<maps::RegEntry> entries = maps::entriesFor(id);
    if (entries.isEmpty()) {
        // Stub map (12DO / 4RTD) — not modelled yet.
        setStatus(QStringLiteral("Карта этого модуля ещё не реализована "
                                 "(заглушка). Используйте свободный режим."),
                  true);
        return;
    }
    buildMapRows(entries);
    setStatus(QStringLiteral("Карта 12DI загружена (%1 регистров)").arg(entries.size()));
}

void OnlineTab::buildFreeRows()
{
    m_table->setRowCount(kFreeRowCount);
    for (int row = 0; row < kFreeRowCount; ++row) {
        Row info;
        info.addrEditable = true;
        info.writable = true; // free-mode writes gated by the FC selector
        m_rows.push_back(info);

        // Name: empty (free mode).
        auto *name = new QTableWidgetItem(QString());
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, ColName, name);

        // Type: mirrors the global selector; shown as read-only text.
        auto *type = new QTableWidgetItem(QStringLiteral("FCxx"));
        type->setFlags(type->flags() & ~Qt::ItemIsEditable);
        type->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, ColType, type);

        // Address: editable spin box, defaults 0..11.
        auto *addr = new QSpinBox;
        addr->setRange(0, 65535);
        addr->setValue(row < 12 ? row : 0);
        m_table->setCellWidget(row, ColAddr, addr);

        for (int col : {ColDec, ColHex, ColDecoded}) {
            auto *item = new QTableWidgetItem(QStringLiteral("—"));
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            item->setTextAlignment(Qt::AlignCenter);
            m_table->setItem(row, col, item);
        }

        auto *write = new QLineEdit;
        write->setPlaceholderText(QStringLiteral("dec/0x.."));
        m_table->setCellWidget(row, ColWrite, write);
    }
}

void OnlineTab::buildMapRows(const QVector<maps::RegEntry> &entries)
{
    m_table->setRowCount(entries.size());
    for (int row = 0; row < entries.size(); ++row) {
        const maps::RegEntry &e = entries.at(row);
        Row info;
        info.addrEditable = false;
        info.addr = e.addr;
        info.type = e.type;
        info.fmt = e.fmt;
        info.writable = e.writable;
        info.decode = e.decode;
        m_rows.push_back(info);

        auto *name = new QTableWidgetItem(e.name);
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, ColName, name);

        QString typeText = e.type == maps::RegEntry::Input ? QStringLiteral("FC04")
                                                           : QStringLiteral("FC03");
        if (e.fmt == maps::RegEntry::F32)
            typeText += QStringLiteral("·f32");
        auto *type = new QTableWidgetItem(typeText);
        type->setFlags(type->flags() & ~Qt::ItemIsEditable);
        type->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, ColType, type);

        auto *addr = new QTableWidgetItem(QString::number(e.addr));
        addr->setFlags(addr->flags() & ~Qt::ItemIsEditable);
        addr->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, ColAddr, addr);

        for (int col : {ColDec, ColHex, ColDecoded}) {
            auto *item = new QTableWidgetItem(QStringLiteral("—"));
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            item->setTextAlignment(Qt::AlignCenter);
            m_table->setItem(row, col, item);
        }

        if (e.writable) {
            auto *write = new QLineEdit;
            write->setPlaceholderText(e.writeHint.isEmpty()
                                          ? QStringLiteral("dec/0x..")
                                          : e.writeHint);
            if (!e.writeHint.isEmpty())
                write->setToolTip(e.writeHint);
            m_table->setCellWidget(row, ColWrite, write);
        } else {
            auto *ro = new QTableWidgetItem(QStringLiteral("—"));
            ro->setFlags(ro->flags() & ~Qt::ItemIsEditable);
            ro->setTextAlignment(Qt::AlignCenter);
            m_table->setItem(row, ColWrite, ro);
        }
    }
}

void OnlineTab::onMapChanged(int)
{
    rebuildTable();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
QSpinBox *OnlineTab::addrSpin(int row) const
{
    return qobject_cast<QSpinBox *>(m_table->cellWidget(row, ColAddr));
}

void OnlineTab::setStatus(const QString &text, bool error)
{
    m_status->setText(text);
    m_status->setStyleSheet(error ? QStringLiteral("color:#b00020;")
                                  : QStringLiteral("color:#006400;"));
}

// ---------------------------------------------------------------------------
// Read / write
// ---------------------------------------------------------------------------
void OnlineTab::onRead()
{
    if (m_rows.isEmpty()) {
        setStatus(QStringLiteral("Нет регистров для чтения"), true);
        return;
    }

    ModbusTcpClient c(2500);
    if (!c.connectToServer(m_ip->text().trimmed(), quint16(m_port->value()),
                           quint8(m_unitId->value()))) {
        setStatus(c.lastError(), true);
        return;
    }

    const bool freeHolding = (m_func->currentIndex() == 1);
    int okCount = 0;

    for (int row = 0; row < m_rows.size(); ++row) {
        const Row &info = m_rows.at(row);
        const quint16 addr = info.addrEditable
                                 ? quint16(addrSpin(row)->value())
                                 : info.addr;
        const bool holding = info.addrEditable
                                 ? freeHolding
                                 : (info.type == maps::RegEntry::Holding);

        // Keep the free-mode Type column in sync with the selector.
        if (info.addrEditable)
            m_table->item(row, ColType)->setText(holding ? QStringLiteral("FC03")
                                                          : QStringLiteral("FC04"));

        const quint16 count = (info.fmt == maps::RegEntry::F32) ? 2 : 1;
        QVector<quint16> regs;
        const bool ok = holding ? c.readHoldingRegisters(addr, count, regs)
                                : c.readInputRegisters(addr, count, regs);
        if (!ok || regs.size() < count) {
            m_table->item(row, ColDec)->setText(QStringLiteral("ERR"));
            m_table->item(row, ColHex)->setText(QStringLiteral("ERR"));
            m_table->item(row, ColDecoded)->setText(QStringLiteral("—"));
            continue;
        }
        if (info.fmt == maps::RegEntry::F32) {
            const float f = regsToFloat(regs[0], regs[1]);
            const quint32 bits = (quint32(regs[0]) << 16) | quint32(regs[1]);
            m_table->item(row, ColDec)->setText(
                std::isnan(f) ? QStringLiteral("NaN") : QString::number(f, 'g', 7));
            m_table->item(row, ColHex)->setText(
                QStringLiteral("0x%1").arg(bits, 8, 16, QChar('0')).toUpper());
            m_table->item(row, ColDecoded)->setText(QStringLiteral("float32"));
        } else {
            const quint16 v = regs.first();
            m_table->item(row, ColDec)->setText(QString::number(v));
            m_table->item(row, ColHex)->setText(
                QStringLiteral("0x%1").arg(v, 4, 16, QChar('0')).toUpper());
            m_table->item(row, ColDecoded)
                ->setText(info.decode ? info.decode(v) : QStringLiteral("—"));
        }
        ++okCount;
    }
    setStatus(QStringLiteral("Прочитано %1/%2 регистров").arg(okCount).arg(m_rows.size()),
              okCount != m_rows.size());
}

void OnlineTab::onWrite()
{
    if (m_rows.isEmpty()) {
        setStatus(QStringLiteral("Нет регистров для записи"), true);
        return;
    }

    const maps::MapId id = maps::mapIdForIndex(m_map->currentIndex());
    if (id == maps::MapId::Free && m_func->currentIndex() != 1) {
        setStatus(QStringLiteral("Запись возможна только для Holding (FC03/06)"), true);
        return;
    }

    ModbusTcpClient c(2500);
    if (!c.connectToServer(m_ip->text().trimmed(), quint16(m_port->value()),
                           quint8(m_unitId->value()))) {
        setStatus(c.lastError(), true);
        return;
    }

    int written = 0;
    for (int row = 0; row < m_rows.size(); ++row) {
        const Row &info = m_rows.at(row);
        if (!info.writable)
            continue;
        auto *edit = qobject_cast<QLineEdit *>(m_table->cellWidget(row, ColWrite));
        if (!edit)
            continue;
        const QString text = edit->text().trimmed();
        if (text.isEmpty())
            continue;
        const quint16 addr = info.addrEditable
                                 ? quint16(addrSpin(row)->value())
                                 : info.addr;

        if (info.fmt == maps::RegEntry::F32) {
            // Float32 registers: parse a decimal value, write two registers
            // (HIGH word first) with FC16.
            bool okf = false;
            const float f = text.toFloat(&okf);
            if (!okf) {
                setStatus(QStringLiteral("Строка %1: неверное float-значение '%2'").arg(row + 1).arg(text), true);
                return;
            }
            quint16 hi = 0, lo = 0;
            floatToRegs(f, hi, lo);
            if (!c.writeMultipleRegisters(addr, {hi, lo})) {
                setStatus(QStringLiteral("Строка %1: %2").arg(row + 1).arg(c.lastError()), true);
                return;
            }
            ++written;
            continue;
        }

        bool ok = false;
        const uint value = text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)
                               ? text.mid(2).toUInt(&ok, 16)
                               : text.toUInt(&ok, 10);
        if (!ok || value > 0xFFFF) {
            setStatus(QStringLiteral("Строка %1: неверное значение '%2'").arg(row + 1).arg(text), true);
            return;
        }
        if (!c.writeSingleRegister(addr, quint16(value))) {
            setStatus(QStringLiteral("Строка %1: %2").arg(row + 1).arg(c.lastError()), true);
            return;
        }
        ++written;
    }
    setStatus(QStringLiteral("Записано регистров: %1").arg(written), false);
}
