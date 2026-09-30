#include "ModuleMaps.h"

#include <QStringList>

namespace maps {

// ---- Decoders --------------------------------------------------------------
namespace {

QString decodeBool01(quint16 v)
{
    return v ? QStringLiteral("Да") : QStringLiteral("Нет");
}

// HR116 on every module: 0 = static, 1 = DHCP, 2 = link-local (factory).
QString decodeNetMode(quint16 v)
{
    switch (v) {
    case 0: return QStringLiteral("static");
    case 1: return QStringLiteral("DHCP");
    case 2: return QStringLiteral("link-local");
    default: return QStringLiteral("?");
    }
}

QString decodeLedMode(quint16 v)
{
    switch (v) {
    case 0: return QStringLiteral("ALW_OFF");
    case 1: return QStringLiteral("ALW_ON");
    case 2: return QStringLiteral("STATE_MACHINE");
    default: return QStringLiteral("?");
    }
}

QString decodeModuleId(quint16 v)
{
    switch (v) {
    case 0x12D1: return QStringLiteral("12DI");
    case 0x12D0: return QStringLiteral("12DO");
    case 0x04D1: return QStringLiteral("4RTD");
    case 0x08AC: return QStringLiteral("8AIC");
    case 0x08A0: return QStringLiteral("8AOC");
    case 0x04DD: return QStringLiteral("4RD");
    default: return QStringLiteral("0x%1").arg(v, 4, 16, QChar('0')).toUpper();
    }
}

// On-chip temperature: signed 0.1 °C, transmitted as two's-complement uint16.
QString decodeDeciCelsius(quint16 v)
{
    const qint16 t = static_cast<qint16>(v);
    return QStringLiteral("%1 °C").arg(t / 10.0, 0, 'f', 1);
}

QString decodeFilterMs(quint16 v)
{
    return QStringLiteral("%1 мс").arg(v);
}

QString decodeDiState(quint16 v)
{
    return v ? QStringLiteral("ВКЛ") : QStringLiteral("выкл");
}

// 12-bit DI mask (register 124): show DI12..DI1 as a bit string.
QString decodeDiMask(quint16 v)
{
    QString bits;
    for (int i = 11; i >= 0; --i)
        bits.append(((v >> i) & 1u) ? QLatin1Char('1') : QLatin1Char('0'));
    return QStringLiteral("DI12..DI1: %1").arg(bits);
}

// 12-bit DQ output mask (register 124 on 12DO).
QString decodeDqMask(quint16 v)
{
    QString bits;
    for (int i = 11; i >= 0; --i)
        bits.append(((v >> i) & 1u) ? QLatin1Char('1') : QLatin1Char('0'));
    return QStringLiteral("DQ12..DQ1: %1").arg(bits);
}

// DQ output value echo (0/1).
QString decodeDqState(quint16 v)
{
    return v ? QStringLiteral("ВКЛ") : QStringLiteral("выкл");
}

// DQ comms-loss behaviour: 0=HOLD, 1=ZERO, 2=SAFE.
QString decodeDqLossMode(quint16 v)
{
    switch (v) {
    case 0: return QStringLiteral("HOLD (удержать)");
    case 1: return QStringLiteral("ZERO (в 0)");
    case 2: return QStringLiteral("SAFE (безопасн.)");
    default: return QStringLiteral("?");
    }
}

// DQ comms-loss timeout in units of 100 ms.
QString decodeDqTimeout(quint16 v)
{
    return v == 0 ? QStringLiteral("немедленно")
                  : QStringLiteral("%1 мс").arg(int(v) * 100);
}

// ---- 4RTD decoders ----
// RTD sensor type code (rtd_type_t, order per Application/rtd/rtd_scales.h).
QString decodeRtdType(quint16 v)
{
    static const char *kNames[] = {
        "50М", "Cu50", "50П", "Pt50", "Ni100", "100М", "Cu100", "100П",
        "Pt100", "Ni500", "500М", "Cu500", "500П", "Pt500", "Ni1000",
        "1000М", "Cu1000", "1000П", "Pt1000", "Res 0..200Ω", "Res 0..2kΩ",
    };
    if (v < (sizeof(kNames) / sizeof(kNames[0])))
        return QString::fromUtf8(kNames[v]);
    return QStringLiteral("?");
}

// Compact-block reading (HR 0..3): int16, °C×100 for RTD types or 0..32767
// of the resistance-mode full scale; 0 = disabled, -32768 = fault.
QString decodeRtdReading(quint16 v)
{
    const qint16 s = qint16(v);
    if (s == -32768) return QStringLiteral("FAULT");
    if (s == 0)      return QStringLiteral("0 (выкл / 0.00)");
    return QStringLiteral("%1 °C  |  %2 %% FS")
        .arg(double(s) / 100.0, 0, 'f', 2)
        .arg(double(s) / 32767.0 * 100.0, 0, 'f', 2);
}

QString decodeAlphaMode(quint16 v)
{
    return v ? QStringLiteral("custom W100") : QStringLiteral("по умолчанию");
}

// Gain class (ADS1220 PGA): FS = 4000 Ω / gain.
static const char *kGclassNames[] = {
    "0: 50Ω  (PGA16, FS 250Ω)", "1: 100Ω (PGA8, FS 500Ω)", "2: 500Ω (PGA2, FS 2kΩ)",
    "3: 1000Ω (PGA1, FS 4kΩ)",  "4: R2k  (PGA1, FS 4kΩ)",
};

QString decodeCalRange(quint16 v)
{
    if (v == 0) return QStringLiteral("auto");
    if (v >= 1 && v <= 5) return QStringLiteral("force ") + QString::fromUtf8(kGclassNames[v - 1]);
    return QStringLiteral("?");
}

QString decodeSmoothing(quint16 v)
{
    switch (v) {
    case 0: return QStringLiteral("выкл");
    case 1: return QStringLiteral("слабое (1/4)");
    case 2: return QStringLiteral("среднее (1/8)");
    case 3: return QStringLiteral("сильное (1/16)");
    default: return QStringLiteral("?");
    }
}

QString decodeRtdGclass(quint16 v)
{
    return v < 5 ? QString::fromUtf8(kGclassNames[v]) : QStringLiteral("?");
}

// RTD reading status flags (input registers 324..327).
QString decodeRtdFlags(quint16 v)
{
    QStringList parts;
    if (v & 0x0001u) parts << QStringLiteral("enabled");
    if (v & 0x0002u) parts << QStringLiteral("valid");
    if (v & 0x0004u) parts << QStringLiteral("FAULT");
    switch ((v >> 8) & 0xFFu) {
    case 0: break;
    case 1: parts << QStringLiteral("обрыв / выше шкалы"); break;
    case 2: parts << QStringLiteral("КЗ датчика"); break;
    case 3: parts << QStringLiteral("АЦП не отвечает"); break;
    default: parts << QStringLiteral("fault=0x%1").arg((v >> 8) & 0xFFu, 2, 16, QChar('0')).toUpper();
    }
    return parts.isEmpty() ? QStringLiteral("—") : parts.join(QStringLiteral(", "));
}

// Calibration lock bitmask: IR127 = slots 0..15, IR128 = slots 16..19
// (slot = ch*5 + class). Lists the locked slots as CHx/cy.
static QString lockList(quint16 v, int firstSlot, int count)
{
    QStringList locked;
    for (int i = 0; i < count; ++i)
        if ((v >> i) & 1u) {
            const int slot = firstSlot + i;
            locked << QStringLiteral("CH%1/c%2").arg(slot / 5).arg(slot % 5);
        }
    return locked.isEmpty() ? QStringLiteral("нет") : locked.join(QStringLiteral(" "));
}
QString decodeCalLock(quint16 v)   { return lockList(v, 0, 16); }
QString decodeCalLockHi(quint16 v) { return lockList(v, 16, 4); }

// ---- 12DI map --------------------------------------------------------------
// Mirrors Application/modbus/modbus_app.h of PLCJS_ETH_MODULE_12DI.
QVector<RegEntry> build12DI()
{
    using T = RegEntry;
    QVector<RegEntry> e;

    // Input registers (FC04, read-only).
    for (int i = 0; i < 12; ++i) {
        e.push_back(T{QStringLiteral("DI%1 состояние").arg(i + 1),
                      quint16(i), RegEntry::Input, false, decodeDiState, {}});
    }
    e.push_back(T{QStringLiteral("Версия FW (major)"), 120, RegEntry::Input, false, nullptr, {}});
    e.push_back(T{QStringLiteral("Версия FW (minor)"), 121, RegEntry::Input, false, nullptr, {}});
    e.push_back(T{QStringLiteral("Uptime, с (low)"),  122, RegEntry::Input, false, nullptr, {}});
    e.push_back(T{QStringLiteral("Uptime, с (high)"), 123, RegEntry::Input, false, nullptr, {}});
    e.push_back(T{QStringLiteral("Маска DI (12 бит)"), 124, RegEntry::Input, false, decodeDiMask, {}});
    e.push_back(T{QStringLiteral("Module ID"),         125, RegEntry::Input, false, decodeModuleId, {}});

    // Holding registers (FC03/FC06, read/write unless noted).
    e.push_back(T{QStringLiteral("Фильтр DI, мс"),     100, RegEntry::Holding, true, decodeFilterMs, QStringLiteral("10..1000")});
    e.push_back(T{QStringLiteral("Режим LED"),         101, RegEntry::Holding, true, decodeLedMode, QStringLiteral("0=OFF 1=ON 2=SM")});
    e.push_back(T{QStringLiteral("Modbus slave id"),   102, RegEntry::Holding, true, nullptr, QStringLiteral("1..247")});
    e.push_back(T{QStringLiteral("Modbus TCP порт"),   103, RegEntry::Holding, true, nullptr, QStringLiteral(">0")});
    for (int i = 0; i < 4; ++i)
        e.push_back(T{QStringLiteral("IP октет %1").arg(i + 1), quint16(104 + i), RegEntry::Holding, true, nullptr, QStringLiteral("0..255")});
    for (int i = 0; i < 4; ++i)
        e.push_back(T{QStringLiteral("Netmask октет %1").arg(i + 1), quint16(108 + i), RegEntry::Holding, true, nullptr, QStringLiteral("0..255")});
    for (int i = 0; i < 4; ++i)
        e.push_back(T{QStringLiteral("Gateway октет %1").arg(i + 1), quint16(112 + i), RegEntry::Holding, true, nullptr, QStringLiteral("0..255")});
    e.push_back(T{QStringLiteral("Сетевой режим"), 116, RegEntry::Holding, true, decodeNetMode, QStringLiteral("0=static 1=DHCP 2=link-local")});
    e.push_back(T{QStringLiteral("Сохранить (trigger)"),      117, RegEntry::Holding, true, nullptr, QStringLiteral("0xA5A5")});
    e.push_back(T{QStringLiteral("Сервисные команды"), 118, RegEntry::Holding, true, nullptr, QStringLiteral("0xB00B=reboot 0xB007=bootloader 0x8863=сброс switch")});
    e.push_back(T{QStringLiteral("Сброс к заводским (trig.)"), 119, RegEntry::Holding, true, nullptr, QStringLiteral("0xDEAD")});
    e.push_back(T{QStringLiteral("Температура чипа"), 130, RegEntry::Holding, false, decodeDeciCelsius, {}});

    return e;
}

// ---- 12DO map --------------------------------------------------------------
// Mirrors Application/modbus/modbus_app.h of PLCJS_ETH_MODULE_12DQ.
QVector<RegEntry> build12DO()
{
    using T = RegEntry;
    QVector<RegEntry> e;

    // Input registers (FC04, read-only) — output-state echo + info.
    for (int i = 0; i < 12; ++i)
        e.push_back(T{QStringLiteral("DQ%1 состояние (echo)").arg(i + 1),
                      quint16(i), RegEntry::Input, false, decodeDqState, {}});
    e.push_back(T{QStringLiteral("Версия FW (major)"), 120, RegEntry::Input, false, nullptr, {}});
    e.push_back(T{QStringLiteral("Версия FW (minor)"), 121, RegEntry::Input, false, nullptr, {}});
    e.push_back(T{QStringLiteral("Uptime, с (low)"),  122, RegEntry::Input, false, nullptr, {}});
    e.push_back(T{QStringLiteral("Uptime, с (high)"), 123, RegEntry::Input, false, nullptr, {}});
    e.push_back(T{QStringLiteral("Маска DQ (12 бит)"), 124, RegEntry::Input, false, decodeDqMask, {}});
    e.push_back(T{QStringLiteral("Module ID"),         125, RegEntry::Input, false, decodeModuleId, {}});

    // Discrete-output control block (holding, read/write).
    e.push_back(T{QStringLiteral("Групповой выход (маска)"), 50, RegEntry::Holding, true, decodeDqMask, QStringLiteral("битовая маска DQ")});
    for (int i = 0; i < 12; ++i)
        e.push_back(T{QStringLiteral("DQ%1 значение").arg(i + 1), quint16(51 + i), RegEntry::Holding, true, decodeDqState, QStringLiteral("0/1")});
    for (int i = 0; i < 12; ++i)
        e.push_back(T{QStringLiteral("DQ%1 режим при потере связи").arg(i + 1), quint16(63 + i), RegEntry::Holding, true, decodeDqLossMode, QStringLiteral("0=HOLD 1=ZERO 2=SAFE")});
    for (int i = 0; i < 12; ++i)
        e.push_back(T{QStringLiteral("DQ%1 безопасное значение").arg(i + 1), quint16(75 + i), RegEntry::Holding, true, decodeDqState, QStringLiteral("0/1")});
    for (int i = 0; i < 12; ++i)
        e.push_back(T{QStringLiteral("DQ%1 таймаут связи").arg(i + 1), quint16(87 + i), RegEntry::Holding, true, decodeDqTimeout, QStringLiteral("×100 мс, 0=сразу")});

    // Common configuration (holding, read/write unless noted).
    e.push_back(T{QStringLiteral("Режим LED"),         101, RegEntry::Holding, true, decodeLedMode, QStringLiteral("0=OFF 1=ON 2=SM")});
    e.push_back(T{QStringLiteral("Modbus slave id"),   102, RegEntry::Holding, true, nullptr, QStringLiteral("1..247")});
    e.push_back(T{QStringLiteral("Modbus TCP порт"),   103, RegEntry::Holding, true, nullptr, QStringLiteral(">0")});
    for (int i = 0; i < 4; ++i)
        e.push_back(T{QStringLiteral("IP октет %1").arg(i + 1), quint16(104 + i), RegEntry::Holding, true, nullptr, QStringLiteral("0..255")});
    for (int i = 0; i < 4; ++i)
        e.push_back(T{QStringLiteral("Netmask октет %1").arg(i + 1), quint16(108 + i), RegEntry::Holding, true, nullptr, QStringLiteral("0..255")});
    for (int i = 0; i < 4; ++i)
        e.push_back(T{QStringLiteral("Gateway октет %1").arg(i + 1), quint16(112 + i), RegEntry::Holding, true, nullptr, QStringLiteral("0..255")});
    e.push_back(T{QStringLiteral("Сетевой режим"), 116, RegEntry::Holding, true, decodeNetMode, QStringLiteral("0=static 1=DHCP 2=link-local")});
    e.push_back(T{QStringLiteral("Сохранить (trigger)"),      117, RegEntry::Holding, true, nullptr, QStringLiteral("0xA5A5")});
    e.push_back(T{QStringLiteral("Сервисные команды"), 118, RegEntry::Holding, true, nullptr, QStringLiteral("0xB00B=reboot 0xB007=bootloader 0x8863=сброс switch")});
    e.push_back(T{QStringLiteral("Сброс к заводским (trig.)"), 119, RegEntry::Holding, true, nullptr, QStringLiteral("0xDEAD")});

    return e;
}

// ---- 4RTD map (HW2.1, ADS1220) ----------------------------------------------
// Mirrors Application/modbus/modbus_app.h of PLCJS_ETH_MODULE_4RTD (fw 2.x).
// Multi-channel quantities are grouped by quantity: 4 consecutive registers
// (or 4 float/int32 pairs) are channels 1..4.
QVector<RegEntry> build4RTD()
{
    using T = RegEntry;
    QVector<RegEntry> e;

    // Compact holding block 0..27: group*4 + ch.
    for (int ch = 0; ch < 4; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 показание int16").arg(ch + 1), quint16(0 + ch), RegEntry::Holding, false, decodeRtdReading, {}});
    for (int ch = 0; ch < 4; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 тип датчика").arg(ch + 1), quint16(4 + ch), RegEntry::Holding, true, decodeRtdType, QStringLiteral("0..20 (19=R200, 20=R2k)")});
    for (int ch = 0; ch < 4; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 включён").arg(ch + 1), quint16(8 + ch), RegEntry::Holding, true, decodeBool01, QStringLiteral("0/1")});
    for (int ch = 0; ch < 4; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 режим alpha").arg(ch + 1), quint16(12 + ch), RegEntry::Holding, true, decodeAlphaMode, QStringLiteral("0/1")});
    for (int ch = 0; ch < 4; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 custom W100 ×10000").arg(ch + 1), quint16(16 + ch), RegEntry::Holding, true, nullptr, {}});
    for (int ch = 0; ch < 4; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 класс усиления (override)").arg(ch + 1), quint16(20 + ch), RegEntry::Holding, true, decodeCalRange, QStringLiteral("0=auto 1..5=класс 0..4")});
    for (int ch = 0; ch < 4; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 сглаживание (EMA)").arg(ch + 1), quint16(24 + ch), RegEntry::Holding, true, decodeSmoothing, QStringLiteral("0=выкл 1..3")});

    // Readings (input registers 300..339), grouped by quantity.
    for (int ch = 0; ch < 4; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 температура, °C").arg(ch + 1), quint16(300 + ch * 2), RegEntry::Input, false, nullptr, {}, RegEntry::F32});
    for (int ch = 0; ch < 4; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 R калибр., Ом").arg(ch + 1), quint16(308 + ch * 2), RegEntry::Input, false, nullptr, {}, RegEntry::F32});
    for (int ch = 0; ch < 4; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 R сырое, Ом").arg(ch + 1), quint16(316 + ch * 2), RegEntry::Input, false, nullptr, {}, RegEntry::F32});
    for (int ch = 0; ch < 4; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 флаги").arg(ch + 1), quint16(324 + ch), RegEntry::Input, false, decodeRtdFlags, {}});
    for (int ch = 0; ch < 4; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 ADC код (24-bit)").arg(ch + 1), quint16(328 + ch * 2), RegEntry::Input, false, nullptr, {}, RegEntry::I32});
    for (int ch = 0; ch < 4; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 класс усиления").arg(ch + 1), quint16(336 + ch), RegEntry::Input, false, decodeRtdGclass, {}});

    // Global input registers.
    e.push_back(T{QStringLiteral("Версия FW (major)"), 120, RegEntry::Input, false, nullptr, {}});
    e.push_back(T{QStringLiteral("Версия FW (minor)"), 121, RegEntry::Input, false, nullptr, {}});
    e.push_back(T{QStringLiteral("Uptime, с (low)"),  122, RegEntry::Input, false, nullptr, {}});
    e.push_back(T{QStringLiteral("Uptime, с (high)"), 123, RegEntry::Input, false, nullptr, {}});
    e.push_back(T{QStringLiteral("Module ID"),        125, RegEntry::Input, false, decodeModuleId, {}});
    e.push_back(T{QStringLiteral("Температура чипа"),  126, RegEntry::Input, false, decodeDeciCelsius, {}});
    e.push_back(T{QStringLiteral("Блокировка калибровки (слоты 0..15)"), 127, RegEntry::Input, false, decodeCalLock, {}});
    e.push_back(T{QStringLiteral("Блокировка калибровки (слоты 16..19)"), 128, RegEntry::Input, false, decodeCalLockHi, {}});

    // Global holding registers.
    e.push_back(T{QStringLiteral("Период опроса RTD, мс"), 100, RegEntry::Holding, true, decodeFilterMs, QStringLiteral("50..5000")});
    e.push_back(T{QStringLiteral("Режим LED"),         101, RegEntry::Holding, true, decodeLedMode, QStringLiteral("0=OFF 1=ON 2=SM")});
    e.push_back(T{QStringLiteral("Modbus slave id"),   102, RegEntry::Holding, true, nullptr, QStringLiteral("1..247")});
    e.push_back(T{QStringLiteral("Modbus TCP порт"),   103, RegEntry::Holding, true, nullptr, QStringLiteral(">0")});
    for (int i = 0; i < 4; ++i)
        e.push_back(T{QStringLiteral("IP октет %1").arg(i + 1), quint16(104 + i), RegEntry::Holding, true, nullptr, QStringLiteral("0..255")});
    for (int i = 0; i < 4; ++i)
        e.push_back(T{QStringLiteral("Netmask октет %1").arg(i + 1), quint16(108 + i), RegEntry::Holding, true, nullptr, QStringLiteral("0..255")});
    for (int i = 0; i < 4; ++i)
        e.push_back(T{QStringLiteral("Gateway октет %1").arg(i + 1), quint16(112 + i), RegEntry::Holding, true, nullptr, QStringLiteral("0..255")});
    e.push_back(T{QStringLiteral("Сетевой режим"), 116, RegEntry::Holding, true, decodeNetMode, QStringLiteral("0=static 1=DHCP 2=link-local")});
    e.push_back(T{QStringLiteral("Сохранить (trigger)"),      117, RegEntry::Holding, true, nullptr, QStringLiteral("0xA5A5")});
    e.push_back(T{QStringLiteral("Сервисные команды (trigger)"), 118, RegEntry::Holding, true, nullptr, QStringLiteral("0xB00B reboot / 0xB007 boot / 0x8863 KSZ reset")});
    e.push_back(T{QStringLiteral("Сброс к заводским (trig.)"), 119, RegEntry::Holding, true, nullptr, QStringLiteral("0xDEAD")});
    e.push_back(T{QStringLiteral("Температура чипа (HR)"), 130, RegEntry::Holding, false, decodeDeciCelsius, {}});
    e.push_back(T{QStringLiteral("Калибровка: COMMIT"), 131, RegEntry::Holding, true, nullptr, QStringLiteral("0xCA00|slot = 51712+slot, slot=ch*5+class (0..19), НЕОБРАТИМО")});
    e.push_back(T{QStringLiteral("Калибровка: ERASE ARM"), 132, RegEntry::Holding, true, nullptr, QStringLiteral("0xC1A5")});

    // Calibration coefficients (holding float32, base 540 + ch*20 + class*4).
    for (int ch = 0; ch < 4; ++ch) {
        for (int cls = 0; cls < 5; ++cls) {
            const quint16 b = quint16(540 + ch * 20 + cls * 4);
            e.push_back(T{QStringLiteral("Кан.%1 gain класс %2").arg(ch + 1).arg(cls),   quint16(b + 0), RegEntry::Holding, true, nullptr, QStringLiteral("float"), RegEntry::F32});
            e.push_back(T{QStringLiteral("Кан.%1 offset класс %2").arg(ch + 1).arg(cls), quint16(b + 2), RegEntry::Holding, true, nullptr, QStringLiteral("float"), RegEntry::F32});
        }
    }

    // Nominal reference resistor (holding float32).
    e.push_back(T{QStringLiteral("RREF ном., Ом"), 620, RegEntry::Holding, true, nullptr, QStringLiteral("float, 2000.0"), RegEntry::F32});

    return e;
}

// ---- 8AIC map (8x 4-20 mA, two ADS1220) -------------------------------------
// Mirrors Application/modbus/modbus_app.h of PLCJS_ETH_MODULE_8AIC (fw 1.2+).
// Multi-channel quantities are grouped by quantity: 8 consecutive registers
// (or 8 float/int32 pairs) are channels 1..8.
// Reading is scaled onto the channel's [low, high] thresholds (HR 8..23), so
// only the percent of span can be decoded without knowing them.
QString decodeAicReading(quint16 v)
{
    const qint16 s = qint16(v);
    if (s == -32768) return QStringLiteral("FAULT");
    if (s == 0)      return QStringLiteral("0 (выкл / нижний порог)");
    return QStringLiteral("%1 % шкалы").arg(double(s) / 32767.0 * 100.0, 0, 'f', 2);
}

QString decodeAicMicroamps(quint16 v)
{
    return QStringLiteral("%1 mA").arg(double(v) / 1000.0, 0, 'f', 3);
}

QString decodeAicRate(quint16 v)
{
    switch (v) {
    case 0: return QStringLiteral("20 SPS + FIR 50/60 Гц");
    case 1: return QStringLiteral("90 SPS");
    case 2: return QStringLiteral("330 SPS");
    default: return QStringLiteral("?");
    }
}

QString decodeAicFlags(quint16 v)
{
    QStringList parts;
    if (v & 0x0001u) parts << QStringLiteral("enabled");
    if (v & 0x0002u) parts << QStringLiteral("valid");
    if (v & 0x0004u) parts << QStringLiteral("FAULT");
    switch ((v >> 8) & 0xFFu) {
    case 0: break;
    case 1: parts << QStringLiteral("обрыв (< 3.6 mA)"); break;
    case 2: parts << QStringLiteral("перегрузка (> 21 mA)"); break;
    case 3: parts << QStringLiteral("АЦП не отвечает"); break;
    default: parts << QStringLiteral("fault=0x%1").arg((v >> 8) & 0xFFu, 2, 16, QChar('0')).toUpper();
    }
    return parts.isEmpty() ? QStringLiteral("—") : parts.join(QStringLiteral(", "));
}

QString decodeAicCalLock(quint16 v)
{
    QStringList locked;
    for (int ch = 0; ch < 8; ++ch)
        if ((v >> ch) & 1u) locked << QStringLiteral("CH%1").arg(ch + 1);
    return locked.isEmpty() ? QStringLiteral("нет") : locked.join(QStringLiteral(" "));
}

QVector<RegEntry> build8AIC()
{
    using T = RegEntry;
    QVector<RegEntry> e;

    // Compact holding block 0..47: group*8 + ch.
    for (int ch = 0; ch < 8; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 показание int16 (0..32767 = порог↓..порог↑)").arg(ch + 1), quint16(0 + ch), RegEntry::Holding, false, decodeAicReading, {}});
    for (int ch = 0; ch < 8; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 нижний порог шкалы, мкА").arg(ch + 1), quint16(8 + ch), RegEntry::Holding, true, decodeAicMicroamps, QStringLiteral("0..25000, < верхнего (деф. 4000)")});
    for (int ch = 0; ch < 8; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 верхний порог шкалы, мкА").arg(ch + 1), quint16(16 + ch), RegEntry::Holding, true, decodeAicMicroamps, QStringLiteral("..25000, > нижнего (деф. 20000)")});
    for (int ch = 0; ch < 8; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 включён").arg(ch + 1), quint16(24 + ch), RegEntry::Holding, true, decodeBool01, QStringLiteral("0/1")});
    for (int ch = 0; ch < 8; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 сглаживание (EMA)").arg(ch + 1), quint16(32 + ch), RegEntry::Holding, true, decodeSmoothing, QStringLiteral("0=выкл 1..3")});

    // Readings (input registers 300..355), grouped by quantity.
    for (int ch = 0; ch < 8; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 ток, mA").arg(ch + 1), quint16(300 + ch * 2), RegEntry::Input, false, nullptr, {}, RegEntry::F32});
    for (int ch = 0; ch < 8; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 ток сырой, mA").arg(ch + 1), quint16(316 + ch * 2), RegEntry::Input, false, nullptr, {}, RegEntry::F32});
    for (int ch = 0; ch < 8; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 флаги").arg(ch + 1), quint16(332 + ch), RegEntry::Input, false, decodeAicFlags, {}});
    for (int ch = 0; ch < 8; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 ADC код (24-bit)").arg(ch + 1), quint16(340 + ch * 2), RegEntry::Input, false, nullptr, {}, RegEntry::I32});

    // Global input registers.
    e.push_back(T{QStringLiteral("Версия FW (major)"), 120, RegEntry::Input, false, nullptr, {}});
    e.push_back(T{QStringLiteral("Версия FW (minor)"), 121, RegEntry::Input, false, nullptr, {}});
    e.push_back(T{QStringLiteral("Uptime, с (low)"),  122, RegEntry::Input, false, nullptr, {}});
    e.push_back(T{QStringLiteral("Uptime, с (high)"), 123, RegEntry::Input, false, nullptr, {}});
    e.push_back(T{QStringLiteral("Module ID"),        125, RegEntry::Input, false, decodeModuleId, {}});
    e.push_back(T{QStringLiteral("Температура чипа"),  126, RegEntry::Input, false, decodeDeciCelsius, {}});
    e.push_back(T{QStringLiteral("Блокировка калибровки"), 127, RegEntry::Input, false, decodeAicCalLock, {}});

    // Global holding registers.
    e.push_back(T{QStringLiteral("Период опроса, мс"), 100, RegEntry::Holding, true, decodeFilterMs, QStringLiteral("50..5000")});
    e.push_back(T{QStringLiteral("Режим LED"),         101, RegEntry::Holding, true, decodeLedMode, QStringLiteral("0=OFF 1=ON 2=SM")});
    e.push_back(T{QStringLiteral("Modbus slave id"),   102, RegEntry::Holding, true, nullptr, QStringLiteral("1..247")});
    e.push_back(T{QStringLiteral("Modbus TCP порт"),   103, RegEntry::Holding, true, nullptr, QStringLiteral(">0")});
    for (int i = 0; i < 4; ++i)
        e.push_back(T{QStringLiteral("IP октет %1").arg(i + 1), quint16(104 + i), RegEntry::Holding, true, nullptr, QStringLiteral("0..255")});
    for (int i = 0; i < 4; ++i)
        e.push_back(T{QStringLiteral("Netmask октет %1").arg(i + 1), quint16(108 + i), RegEntry::Holding, true, nullptr, QStringLiteral("0..255")});
    for (int i = 0; i < 4; ++i)
        e.push_back(T{QStringLiteral("Gateway октет %1").arg(i + 1), quint16(112 + i), RegEntry::Holding, true, nullptr, QStringLiteral("0..255")});
    e.push_back(T{QStringLiteral("Сетевой режим"), 116, RegEntry::Holding, true, decodeNetMode, QStringLiteral("0=static 1=DHCP 2=link-local")});
    e.push_back(T{QStringLiteral("Сохранить (trigger)"),      117, RegEntry::Holding, true, nullptr, QStringLiteral("0xA5A5")});
    e.push_back(T{QStringLiteral("Сервисные команды (trigger)"), 118, RegEntry::Holding, true, nullptr, QStringLiteral("0xB00B reboot / 0xB007 boot / 0x8863 KSZ reset")});
    e.push_back(T{QStringLiteral("Сброс к заводским (trig.)"), 119, RegEntry::Holding, true, nullptr, QStringLiteral("0xDEAD")});
    e.push_back(T{QStringLiteral("Температура чипа (HR)"), 130, RegEntry::Holding, false, decodeDeciCelsius, {}});
    e.push_back(T{QStringLiteral("Калибровка: COMMIT"), 131, RegEntry::Holding, true, nullptr, QStringLiteral("0xCA00|ch = 51712+ch (0..7), НЕОБРАТИМО")});
    e.push_back(T{QStringLiteral("Калибровка: ERASE ARM"), 132, RegEntry::Holding, true, nullptr, QStringLiteral("0xC1A5")});
    e.push_back(T{QStringLiteral("Скорость АЦП"), 133, RegEntry::Holding, true, decodeAicRate, QStringLiteral("0=20SPS+FIR 1=90SPS 2=330SPS")});

    // Calibration coefficients (holding float32, base 540 + ch*4).
    for (int ch = 0; ch < 8; ++ch) {
        const quint16 b = quint16(540 + ch * 4);
        e.push_back(T{QStringLiteral("Кан.%1 gain").arg(ch + 1),       quint16(b + 0), RegEntry::Holding, true, nullptr, QStringLiteral("float"), RegEntry::F32});
        e.push_back(T{QStringLiteral("Кан.%1 offset, mA").arg(ch + 1), quint16(b + 2), RegEntry::Holding, true, nullptr, QStringLiteral("float"), RegEntry::F32});
    }

    // Nominal shunt / reference (holding float32).
    e.push_back(T{QStringLiteral("R_shunt ном., Ом"), 620, RegEntry::Holding, true, nullptr, QStringLiteral("float, 89.9"), RegEntry::F32});
    e.push_back(T{QStringLiteral("V_REF ном., В"),    622, RegEntry::Holding, true, nullptr, QStringLiteral("float, 2.048"), RegEntry::F32});

    return e;
}

// ---- 8AOC map (8x 0-20 mA outputs, DAC80508 + XTR111) -------------------------
// Mirrors Application/modbus/modbus_app.h of PLCJS_ETH_MODULE_8AOC (fw 1.x).
QString decodeAocSetpoint(quint16 v)
{
    const qint16 s = qint16(v);
    return QStringLiteral("%1 % шкалы").arg(double(s) / 32767.0 * 100.0, 0, 'f', 2);
}

QString decodeAocLossMode(quint16 v)
{
    switch (v) {
    case 0: return QStringLiteral("HOLD");
    case 1: return QStringLiteral("SAFE (→ safe-значение)");
    case 2: return QStringLiteral("OFF");
    default: return QStringLiteral("?");
    }
}

QString decodeAocFlags(quint16 v)
{
    QStringList parts;
    if (v & 0x0001u) parts << QStringLiteral("enabled");
    if (v & 0x0002u) parts << QStringLiteral("output ON");
    if (v & 0x0004u) parts << QStringLiteral("FAULT");
    if (v & 0x0008u) parts << QStringLiteral("COMMS-LOSS");
    switch ((v >> 8) & 0xFFu) {
    case 0: break;
    case 1: parts << QStringLiteral("EF: обрыв петли / диапазон / перегрев"); break;
    case 2: parts << QStringLiteral("расширитель не отвечает"); break;
    case 3: parts << QStringLiteral("ЦАП не отвечает"); break;
    case 4: parts << QStringLiteral("питание петли выключено"); break;
    default: parts << QStringLiteral("fault=0x%1").arg((v >> 8) & 0xFFu, 2, 16, QChar('0')).toUpper();
    }
    return parts.isEmpty() ? QStringLiteral("—") : parts.join(QStringLiteral(", "));
}

QString decodeAocTimeout(quint16 v)
{
    return v == 0 ? QStringLiteral("выкл") : QStringLiteral("%1 с").arg(double(v) / 10.0, 0, 'f', 1);
}

QVector<RegEntry> build8AOC()
{
    using T = RegEntry;
    QVector<RegEntry> e;

    // Compact holding block 0..55: group*8 + ch.
    for (int ch = 0; ch < 8; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 уставка int16 (0..32767 = порог↓..порог↑)").arg(ch + 1), quint16(0 + ch), RegEntry::Holding, true, decodeAocSetpoint, QStringLiteral("0..32767")});
    for (int ch = 0; ch < 8; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 нижний порог шкалы, мкА").arg(ch + 1), quint16(8 + ch), RegEntry::Holding, true, decodeAicMicroamps, QStringLiteral("0..22000, < верхнего (деф. 4000)")});
    for (int ch = 0; ch < 8; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 верхний порог шкалы, мкА").arg(ch + 1), quint16(16 + ch), RegEntry::Holding, true, decodeAicMicroamps, QStringLiteral("..22000, > нижнего (деф. 20000)")});
    for (int ch = 0; ch < 8; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 включён").arg(ch + 1), quint16(24 + ch), RegEntry::Holding, true, decodeBool01, QStringLiteral("0/1")});
    for (int ch = 0; ch < 8; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 режим при потере связи").arg(ch + 1), quint16(32 + ch), RegEntry::Holding, true, decodeAocLossMode, QStringLiteral("0=HOLD 1=SAFE 2=OFF")});
    for (int ch = 0; ch < 8; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 safe-значение, мкА").arg(ch + 1), quint16(40 + ch), RegEntry::Holding, true, decodeAicMicroamps, QStringLiteral("0..22000")});
    for (int ch = 0; ch < 8; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 уставка, мкА").arg(ch + 1), quint16(48 + ch), RegEntry::Holding, true, decodeAicMicroamps, QStringLiteral("0..22000")});

    // Status (input registers 300..333).
    for (int ch = 0; ch < 8; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 командуемый ток, mA").arg(ch + 1), quint16(300 + ch * 2), RegEntry::Input, false, nullptr, {}, RegEntry::F32});
    for (int ch = 0; ch < 8; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 флаги").arg(ch + 1), quint16(316 + ch), RegEntry::Input, false, decodeAocFlags, {}});
    for (int ch = 0; ch < 8; ++ch)
        e.push_back(T{QStringLiteral("Кан.%1 код ЦАП").arg(ch + 1), quint16(324 + ch), RegEntry::Input, false, nullptr, {}});
    e.push_back(T{QStringLiteral("Питание петли включено"), 332, RegEntry::Input, false, decodeBool01, {}});
    e.push_back(T{QStringLiteral("Потеря связи активна"), 333, RegEntry::Input, false, decodeBool01, {}});

    // Global input registers.
    e.push_back(T{QStringLiteral("Версия FW (major)"), 120, RegEntry::Input, false, nullptr, {}});
    e.push_back(T{QStringLiteral("Версия FW (minor)"), 121, RegEntry::Input, false, nullptr, {}});
    e.push_back(T{QStringLiteral("Uptime, с (low)"),  122, RegEntry::Input, false, nullptr, {}});
    e.push_back(T{QStringLiteral("Uptime, с (high)"), 123, RegEntry::Input, false, nullptr, {}});
    e.push_back(T{QStringLiteral("Module ID"),        125, RegEntry::Input, false, decodeModuleId, {}});
    e.push_back(T{QStringLiteral("Температура чипа"),  126, RegEntry::Input, false, decodeDeciCelsius, {}});
    e.push_back(T{QStringLiteral("Блокировка калибровки"), 127, RegEntry::Input, false, decodeAicCalLock, {}});

    // Global holding registers.
    e.push_back(T{QStringLiteral("Таймаут потери связи, ×100 мс"), 100, RegEntry::Holding, true, decodeAocTimeout, QStringLiteral("0=выкл, 1..6000 (деф. 50)")});
    e.push_back(T{QStringLiteral("Режим LED"),         101, RegEntry::Holding, true, decodeLedMode, QStringLiteral("0=OFF 1=ON 2=SM")});
    e.push_back(T{QStringLiteral("Modbus slave id"),   102, RegEntry::Holding, true, nullptr, QStringLiteral("1..247")});
    e.push_back(T{QStringLiteral("Modbus TCP порт"),   103, RegEntry::Holding, true, nullptr, QStringLiteral(">0")});
    for (int i = 0; i < 4; ++i)
        e.push_back(T{QStringLiteral("IP октет %1").arg(i + 1), quint16(104 + i), RegEntry::Holding, true, nullptr, QStringLiteral("0..255")});
    for (int i = 0; i < 4; ++i)
        e.push_back(T{QStringLiteral("Netmask октет %1").arg(i + 1), quint16(108 + i), RegEntry::Holding, true, nullptr, QStringLiteral("0..255")});
    for (int i = 0; i < 4; ++i)
        e.push_back(T{QStringLiteral("Gateway октет %1").arg(i + 1), quint16(112 + i), RegEntry::Holding, true, nullptr, QStringLiteral("0..255")});
    e.push_back(T{QStringLiteral("Сетевой режим"), 116, RegEntry::Holding, true, decodeNetMode, QStringLiteral("0=static 1=DHCP 2=link-local")});
    e.push_back(T{QStringLiteral("Сохранить (trigger)"),      117, RegEntry::Holding, true, nullptr, QStringLiteral("0xA5A5 (+ уставки как power-on)")});
    e.push_back(T{QStringLiteral("Сервисные команды (trigger)"), 118, RegEntry::Holding, true, nullptr, QStringLiteral("0xB00B reboot / 0xB007 boot / 0x8863 KSZ / 0xA0FF analog power-cycle")});
    e.push_back(T{QStringLiteral("Сброс к заводским (trig.)"), 119, RegEntry::Holding, true, nullptr, QStringLiteral("0xDEAD")});
    e.push_back(T{QStringLiteral("Температура чипа (HR)"), 130, RegEntry::Holding, false, decodeDeciCelsius, {}});
    e.push_back(T{QStringLiteral("Калибровка: COMMIT"), 131, RegEntry::Holding, true, nullptr, QStringLiteral("0xCA00|ch = 51712+ch (0..7), НЕОБРАТИМО")});
    e.push_back(T{QStringLiteral("Калибровка: ERASE ARM"), 132, RegEntry::Holding, true, nullptr, QStringLiteral("0xC1A5")});
    e.push_back(T{QStringLiteral("Период опроса EF, мс"), 133, RegEntry::Holding, true, decodeFilterMs, QStringLiteral("20..5000")});

    // Calibration coefficients (holding float32, base 540 + ch*4).
    for (int ch = 0; ch < 8; ++ch) {
        const quint16 b = quint16(540 + ch * 4);
        e.push_back(T{QStringLiteral("Кан.%1 gain").arg(ch + 1),       quint16(b + 0), RegEntry::Holding, true, nullptr, QStringLiteral("float"), RegEntry::F32});
        e.push_back(T{QStringLiteral("Кан.%1 offset, mA").arg(ch + 1), quint16(b + 2), RegEntry::Holding, true, nullptr, QStringLiteral("float"), RegEntry::F32});
    }

    // Nominal R_SET / V_REF (holding float32).
    e.push_back(T{QStringLiteral("R_SET ном., Ом"), 620, RegEntry::Holding, true, nullptr, QStringLiteral("float, 1100.0"), RegEntry::F32});
    e.push_back(T{QStringLiteral("V_REF ном., В"),  622, RegEntry::Holding, true, nullptr, QStringLiteral("float, 2.5"), RegEntry::F32});

    return e;
}

} // namespace

// ---- Public API ------------------------------------------------------------
QVector<QString> mapNames()
{
    return {
        QStringLiteral("12DI"),
        QStringLiteral("12DO"),
        QStringLiteral("4RTD"),
        QStringLiteral("8AIC"),
        QStringLiteral("8AOC"),
    };
}

MapId mapIdForIndex(int index)
{
    switch (index) {
    case 0:  return MapId::M12DI;
    case 1:  return MapId::M12DO;
    case 2:  return MapId::M4RTD;
    case 3:  return MapId::M8AIC;
    case 4:  return MapId::M8AOC;
    default: return MapId::M12DI;
    }
}

namespace {

// The write field parses decimal by default and hex only with a "0x" prefix,
// so every magic hint carries both spellings.
QString magicHint(std::initializer_list<std::pair<quint16, const char *>> items)
{
    QStringList parts;
    for (const auto &it : items)
        parts << QStringLiteral("0x%1 = %2 %3")
                     .arg(QString::number(it.first, 16).toUpper().rightJustified(4, QChar('0')))
                     .arg(it.first)
                     .arg(QString::fromUtf8(it.second));
    return parts.join(QStringLiteral("  |  "));
}

// Family-wide trigger registers (HR117/118/119/132) get one-click buttons and
// a dec+hex hint. Module-specific extras (8AOC analog power-cycle) are added
// by the caller. Calibration COMMIT (HR131) deliberately gets no button: it
// needs a slot number and is irreversible.
void attachStandardActions(QVector<RegEntry> &e, bool analogPowerCycle)
{
    for (RegEntry &r : e) {
        if (r.type != RegEntry::Holding || !r.writable) continue;
        switch (r.addr) {
        case 117:
            r.actions = { {QStringLiteral("SAVE"), 0xA5A5} };
            r.writeHint = magicHint({{0xA5A5, "сохранить настройки"}});
            break;
        case 118:
            r.actions = { {QStringLiteral("REBOOT"), 0xB00B},
                          {QStringLiteral("BOOT"), 0xB007},
                          {QStringLiteral("KSZ RST"), 0x8863} };
            if (analogPowerCycle) {
                r.actions.push_back({QStringLiteral("PWR CYCLE"), 0xA0FF});
                r.writeHint = magicHint({{0xB00B, "reboot"}, {0xB007, "bootloader"},
                                         {0x8863, "KSZ8863 reset"}, {0xA0FF, "analog power-cycle"}});
            } else {
                r.writeHint = magicHint({{0xB00B, "reboot"}, {0xB007, "bootloader"},
                                         {0x8863, "KSZ8863 reset"}});
            }
            break;
        case 119:
            r.actions = { {QStringLiteral("FACTORY"), 0xDEAD, true} };
            r.writeHint = magicHint({{0xDEAD, "сброс к заводским"}});
            break;
        case 132:
            r.actions = { {QStringLiteral("ARM ERASE"), 0xC1A5, true} };
            r.writeHint = magicHint({{0xC1A5, "взвести стирание калибровки (затем кнопка на модуле)"}});
            break;
        default:
            break;
        }
    }
}

} // namespace

QVector<RegEntry> entriesFor(MapId id)
{
    QVector<RegEntry> e;
    switch (id) {
    case MapId::M12DI: e = build12DI(); break;
    case MapId::M12DO: e = build12DO(); break;
    case MapId::M4RTD: e = build4RTD(); break;
    case MapId::M8AIC: e = build8AIC(); break;
    case MapId::M8AOC: e = build8AOC(); break;
    case MapId::Free:  return {};
    }
    attachStandardActions(e, id == MapId::M8AOC);
    return e;
}

bool isStub(MapId)
{
    return false; // all known module maps are now implemented
}

} // namespace maps
