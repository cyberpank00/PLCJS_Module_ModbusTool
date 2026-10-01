#include "OnlineTab.h"

#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QToolButton>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
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

OnlineTab::~OnlineTab() = default;

OnlineTab::OnlineTab(QWidget *parent)
    : QWidget(parent)
{
    // ---- Connection controls --------------------------------------------
    m_map = new QComboBox;
    m_map->addItems(maps::mapNames());

    m_ip = new QLineEdit(QStringLiteral("192.168.1.10"));
    m_port = new QSpinBox;
    m_port->setRange(1, 65535);
    m_port->setValue(boot::kDefaultPort);
    m_unitId = new QSpinBox;
    m_unitId->setRange(1, 247);
    m_unitId->setValue(boot::kDefaultUnitId);
    // Kept only for the (now unreachable) free-mode code paths; not shown since
    // the "Свободная" map was removed.
    m_func = new QComboBox(this);
    m_func->addItem(QStringLiteral("Input (FC04)"));
    m_func->addItem(QStringLiteral("Holding (FC03)"));
    m_func->hide();
    m_base = new QComboBox;
    m_base->addItem(QStringLiteral("dec"));
    m_base->addItem(QStringLiteral("hex"));

    auto *connBox = new QGroupBox(QStringLiteral("Подключение"));
    auto *connForm = new QFormLayout(connBox);
    connForm->addRow(QStringLiteral("Карта модуля:"), m_map);
    connForm->addRow(QStringLiteral("IP:"), m_ip);
    connForm->addRow(QStringLiteral("Порт:"), m_port);
    connForm->addRow(QStringLiteral("Unit ID:"), m_unitId);
    connForm->addRow(QStringLiteral("Формат:"), m_base);

    // ---- Register table --------------------------------------------------
    m_table = new QTableWidget(0, ColCount);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("Имя"), QStringLiteral("Тип"), QStringLiteral("Адрес"),
         QStringLiteral("Значение (dec)"), QStringLiteral("Значение (hex)"),
         QStringLiteral("Расшифровка"), QStringLiteral("Запись")});
    m_table->verticalHeader()->setVisible(false);
    auto *hh = m_table->horizontalHeader();
    hh->setSectionResizeMode(ColName, QHeaderView::Fixed);
    hh->resizeSection(ColName, 200);
    hh->setSectionResizeMode(ColDecoded, QHeaderView::Stretch);   /* takes the remainder */
    hh->setMinimumSectionSize(120);                                 /* ...but never below 120 px */
    // "Запись" has a fixed width: the text field plus up to four magic
    // buttons (REBOOT / BOOT / KSZ RST / PWR CYCLE) must never be squeezed by
    // the stretch columns when the window is narrow.
    hh->setSectionResizeMode(ColWrite, QHeaderView::Fixed);
    hh->resizeSection(ColWrite, 420);

    // ---- Buttons + status ------------------------------------------------
    auto *readBtn = new QPushButton(QStringLiteral("Читать"));
    auto *writeBtn = new QPushButton(QStringLiteral("Записать"));
    connect(readBtn, &QPushButton::clicked, this, &OnlineTab::onRead);
    connect(writeBtn, &QPushButton::clicked, this, &OnlineTab::onWrite);
    connect(m_map, &QComboBox::currentIndexChanged, this, &OnlineTab::onMapChanged);

    // ---- Continuous-read controls ---------------------------------------
    m_continuous = new QCheckBox(QStringLiteral("Непрерывное чтение"));
    m_period = new QSpinBox;
    m_period->setRange(50, 60000);
    m_period->setSingleStep(50);
    m_period->setValue(500);
    m_period->setSuffix(QStringLiteral(" мс"));
    m_timeout = new QSpinBox;
    m_timeout->setRange(20, 10000);
    m_timeout->setSingleStep(50);
    m_timeout->setValue(200);
    m_timeout->setSuffix(QStringLiteral(" мс"));

    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &OnlineTab::onRead);
    connect(m_continuous, &QCheckBox::toggled, this, [this](bool on) {
        m_period->setEnabled(!on);
        if (on) {
            m_pollTimer->start(m_period->value());
            onRead(); // immediate first read
        } else {
            m_pollTimer->stop();
            m_pollConn.reset();   // release the module's client slot
        }
    });

    auto *btnRow = new QHBoxLayout;
    btnRow->addWidget(readBtn);
    btnRow->addWidget(writeBtn);
    btnRow->addStretch();
    btnRow->addWidget(m_continuous);
    btnRow->addWidget(new QLabel(QStringLiteral("Период:")));
    btnRow->addWidget(m_period);
    btnRow->addWidget(new QLabel(QStringLiteral("Таймаут:")));
    btnRow->addWidget(m_timeout);

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
        write->setPlaceholderText(QStringLiteral("dec или 0x.."));
        write->setToolTip(QStringLiteral("Десятичное число, либо hex с префиксом 0x"));
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
        else if (e.fmt == maps::RegEntry::I32)
            typeText += QStringLiteral("·i32");
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
            write->setObjectName(QStringLiteral("writeEdit"));
            write->setPlaceholderText(e.writeHint.isEmpty()
                                          ? QStringLiteral("dec или 0x..")
                                          : e.writeHint);
            write->setToolTip(e.writeHint.isEmpty()
                                  ? QStringLiteral("Десятичное число, либо hex с префиксом 0x")
                                  : e.writeHint + QStringLiteral("\nВвод: десятичное число, либо hex с префиксом 0x"));
            if (e.actions.isEmpty()) {
                m_table->setCellWidget(row, ColWrite, write);
            } else {
                // Text field + one small button per magic value. The buttons
                // write immediately (FC06) and bypass the "Записать" pass.
                auto *box = new QWidget;
                auto *lay = new QHBoxLayout(box);
                lay->setContentsMargins(0, 0, 0, 0);
                lay->setSpacing(2);
                lay->addWidget(write, 1);
                for (const maps::MagicAction &a : e.actions) {
                    auto *btn = new QToolButton;
                    btn->setText(a.label);
                    btn->setAutoRaise(false);
                    btn->setToolTip(QStringLiteral("HR%1 ← 0x%2 (%3)%4")
                                        .arg(e.addr)
                                        .arg(QString::number(a.value, 16).toUpper().rightJustified(4, QChar('0')))
                                        .arg(a.value)
                                        .arg(a.dangerous ? QStringLiteral("  — с подтверждением") : QString()));
                    if (a.dangerous)
                        btn->setStyleSheet(QStringLiteral("QToolButton{color:#b00020;font-weight:bold;}"));
                    const quint16 addr = e.addr, value = a.value;
                    const QString label = a.label;
                    const bool dangerous = a.dangerous;
                    connect(btn, &QToolButton::clicked, this, [this, addr, value, label, dangerous]() {
                        writeMagic(addr, value, label, dangerous);
                    });
                    lay->addWidget(btn);
                }
                m_table->setCellWidget(row, ColWrite, box);
            }
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

// The write cell is either the QLineEdit itself or a container holding it
// plus the magic buttons.
QLineEdit *OnlineTab::writeEdit(int row) const
{
    QWidget *w = m_table->cellWidget(row, ColWrite);
    if (!w) return nullptr;
    if (auto *e = qobject_cast<QLineEdit *>(w)) return e;
    return w->findChild<QLineEdit *>(QStringLiteral("writeEdit"));
}

void OnlineTab::writeMagic(quint16 addr, quint16 value, const QString &label, bool dangerous)
{
    if (dangerous) {
        const auto r = QMessageBox::warning(
            this, QStringLiteral("Подтверждение"),
            QStringLiteral("Записать 0x%1 (%2) в HR%3 — %4?\nДействие необратимо / сбрасывает состояние модуля.")
                .arg(QString::number(value, 16).toUpper().rightJustified(4, QChar('0')))
                .arg(value).arg(addr).arg(label),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (r != QMessageBox::Yes) return;
    }

    // Reuse the continuous-read connection when it is open, otherwise a
    // throwaway one — same policy as the "Записать" button.
    std::unique_ptr<ModbusTcpClient> oneShot;
    ModbusTcpClient *c = (m_continuous->isChecked() && m_pollConn && m_pollConn->isConnected())
                             ? m_pollConn.get() : nullptr;
    if (!c) {
        oneShot = std::make_unique<ModbusTcpClient>(2500);
        if (!oneShot->connectToServer(m_ip->text().trimmed(), quint16(m_port->value()),
                                      quint8(m_unitId->value()))) {
            setStatus(oneShot->lastError(), true);
            return;
        }
        c = oneShot.get();
    }
    if (!c->writeSingleRegister(addr, value)) {
        setStatus(QStringLiteral("%1: %2").arg(label).arg(c->lastError()), true);
        return;
    }
    setStatus(QStringLiteral("%1: HR%2 ← 0x%3 (%4) записано")
                  .arg(label).arg(addr)
                  .arg(QString::number(value, 16).toUpper().rightJustified(4, QChar('0')))
                  .arg(value), false);
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

    QElapsedTimer cycle;
    cycle.start();

    // Continuous mode reuses one connection across cycles; a single read gets
    // a throwaway one that closes when this function returns.
    const bool persistent = m_continuous->isChecked();
    std::unique_ptr<ModbusTcpClient> oneShot;
    if (!persistent) {
        oneShot = std::make_unique<ModbusTcpClient>(m_timeout->value());
    } else if (!m_pollConn) {
        m_pollConn = std::make_unique<ModbusTcpClient>(m_timeout->value());
    }
    ModbusTcpClient &c = persistent ? *m_pollConn : *oneShot;
    c.setTimeout(m_timeout->value());

    if (!c.isConnected() &&
        !c.connectToServer(m_ip->text().trimmed(), quint16(m_port->value()),
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

        const quint16 count = (info.fmt != maps::RegEntry::U16) ? 2 : 1;
        QVector<quint16> regs;
        const bool ok = holding ? c.readHoldingRegisters(addr, count, regs)
                                : c.readInputRegisters(addr, count, regs);
        if (!ok || regs.size() < count) {
            m_table->item(row, ColDec)->setText(QStringLiteral("ERR"));
            m_table->item(row, ColHex)->setText(QStringLiteral("ERR"));
            m_table->item(row, ColDecoded)->setText(QStringLiteral("—"));
            if (!c.isConnected() || c.lastErrorIsTransport()) {
                // Transport gone (cable, module reboot, eviction) or a request
                // timed out: a pulled cable leaves the socket "connected", so
                // grinding through every remaining row would cost rows x timeout
                // with the GUI frozen. Stop this cycle, drop the connection and
                // let the next cycle reconnect (which fails fast while the link
                // is down). Copy the message first: reset() destroys c's object.
                const QString err = c.lastError();
                if (persistent) m_pollConn.reset(); else c.close();
                setStatus(QStringLiteral("Соединение потеряно — %1").arg(err), true);
                return;
            }
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
        } else if (info.fmt == maps::RegEntry::I32) {
            const quint32 bits = (quint32(regs[0]) << 16) | quint32(regs[1]);
            m_table->item(row, ColDec)->setText(QString::number(qint32(bits)));
            m_table->item(row, ColHex)->setText(
                QStringLiteral("0x%1").arg(bits, 8, 16, QChar('0')).toUpper());
            m_table->item(row, ColDecoded)->setText(QStringLiteral("int32"));
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
    setStatus(QStringLiteral("Прочитано %1/%2 регистров  •  опрос %3 мс")
                  .arg(okCount).arg(m_rows.size()).arg(cycle.elapsed()),
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

    // The "save settings" trigger (HR117 = 0xA5A5) must be written LAST, after
    // every config register, or a save that sits above a config row in the map
    // would persist the pre-write state and the config change would be lost.
    constexpr quint16 kSaveTrigAddr = 117;
    int saveRow = -1;

    int written = 0;
    for (int row = 0; row < m_rows.size(); ++row) {
        const Row &info = m_rows.at(row);
        if (!info.writable)
            continue;
        auto *edit = writeEdit(row);
        if (!edit)
            continue;
        const QString text = edit->text().trimmed();
        if (text.isEmpty())
            continue;
        const quint16 addr = info.addrEditable
                                 ? quint16(addrSpin(row)->value())
                                 : info.addr;

        // Defer the save trigger; it is written after the loop.
        if (addr == kSaveTrigAddr) { saveRow = row; continue; }

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

    // Deferred save trigger, written last so all config writes are persisted.
    if (saveRow >= 0) {
        auto *edit = writeEdit(saveRow);
        const QString text = edit ? edit->text().trimmed() : QString();
        bool ok = false;
        const uint value = text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)
                               ? text.mid(2).toUInt(&ok, 16)
                               : text.toUInt(&ok, 10);
        if (!ok || value > 0xFFFF) {
            setStatus(QStringLiteral("Строка %1: неверное значение '%2'").arg(saveRow + 1).arg(text), true);
            return;
        }
        if (!c.writeSingleRegister(kSaveTrigAddr, quint16(value))) {
            setStatus(QStringLiteral("Строка %1: %2").arg(saveRow + 1).arg(c.lastError()), true);
            return;
        }
        ++written;
    }

    setStatus(QStringLiteral("Записано регистров: %1").arg(written), false);
}
