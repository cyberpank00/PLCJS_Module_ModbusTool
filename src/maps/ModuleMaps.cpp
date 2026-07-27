#include "ModuleMaps.h"

#include <QStringList>

namespace maps {

// ---- Decoders --------------------------------------------------------------
namespace {

QString decodeBool01(quint16 v)
{
    return v ? QStringLiteral("Да") : QStringLiteral("Нет");
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
        "1000М", "Cu1000", "1000П", "Pt1000", "Res 2kΩ", "Res 5kΩ",
    };
    if (v < (sizeof(kNames) / sizeof(kNames[0])))
        return QString::fromUtf8(kNames[v]);
    return QStringLiteral("?");
}

QString decodeAlphaMode(quint16 v)
{
    return v ? QStringLiteral("custom W100") : QStringLiteral("по умолчанию");
}

QString decodeCalRange(quint16 v)
{
    switch (v) {
    case 0: return QStringLiteral("auto");
    case 1: return QStringLiteral("low RREF");
    case 2: return QStringLiteral("high RREF");
    default: return QStringLiteral("?");
    }
}

QString decodeRtdRange(quint16 v)
{
    return v ? QStringLiteral("high RREF") : QStringLiteral("low RREF");
}

// RTD reading status flags (input register base+6).
QString decodeRtdFlags(quint16 v)
{
    QStringList parts;
    if (v & 0x0001u) parts << QStringLiteral("enabled");
    if (v & 0x0002u) parts << QStringLiteral("valid");
    if (v & 0x0004u) parts << QStringLiteral("FAULT");
    const quint16 fb = (v >> 8) & 0xFFu;
    if (fb)
        parts << QStringLiteral("MAX31865=0x%1").arg(fb, 2, 16, QChar('0')).toUpper();
    return parts.isEmpty() ? QStringLiteral("—") : parts.join(QStringLiteral(", "));
}

// Calibration lock bitmask (input register 127): bit (ch*2+range) committed.
QString decodeCalLock(quint16 v)
{
    QString bits;
    for (int i = 7; i >= 0; --i)
        bits.append(((v >> i) & 1u) ? QLatin1Char('1') : QLatin1Char('0'));
    return QStringLiteral("slots7..0: %1").arg(bits);
}

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
    e.push_back(T{QStringLiteral("Использовать DHCP"), 116, RegEntry::Holding, true, decodeBool01, QStringLiteral("0/1")});
    e.push_back(T{QStringLiteral("Сохранить (trigger)"),      117, RegEntry::Holding, true, nullptr, QStringLiteral("0xA5A5")});
    e.push_back(T{QStringLiteral("Перезагрузка (trigger)"),   118, RegEntry::Holding, true, nullptr, QStringLiteral("0xB00B / 0xB007")});
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
    e.push_back(T{QStringLiteral("Использовать DHCP"), 116, RegEntry::Holding, true, decodeBool01, QStringLiteral("0/1")});
    e.push_back(T{QStringLiteral("Сохранить (trigger)"),      117, RegEntry::Holding, true, nullptr, QStringLiteral("0xA5A5")});
    e.push_back(T{QStringLiteral("Перезагрузка (trigger)"),   118, RegEntry::Holding, true, nullptr, QStringLiteral("0xB00B / 0xB007")});
    e.push_back(T{QStringLiteral("Сброс к заводским (trig.)"), 119, RegEntry::Holding, true, nullptr, QStringLiteral("0xDEAD")});

    return e;
}

// ---- 4RTD map --------------------------------------------------------------
// Mirrors Application/modbus/modbus_app.h of PLCJS_ETH_MODULE_4RTD.
// Float32 values span two registers (HIGH word first) and use fmt = F32.
QVector<RegEntry> build4RTD()
{
    using T = RegEntry;
    QVector<RegEntry> e;

    // Per-channel readings (input registers, base 300 + ch*20).
    for (int ch = 0; ch < 4; ++ch) {
        const quint16 b = quint16(300 + ch * 20);
        const int n = ch + 1;
        e.push_back(T{QStringLiteral("Кан.%1 температура, °C").arg(n), quint16(b + 0), RegEntry::Input, false, nullptr, {}, RegEntry::F32});
        e.push_back(T{QStringLiteral("Кан.%1 R калибр., Ом").arg(n),   quint16(b + 2), RegEntry::Input, false, nullptr, {}, RegEntry::F32});
        e.push_back(T{QStringLiteral("Кан.%1 R сырое, Ом").arg(n),     quint16(b + 4), RegEntry::Input, false, nullptr, {}, RegEntry::F32});
        e.push_back(T{QStringLiteral("Кан.%1 флаги").arg(n),           quint16(b + 6), RegEntry::Input, false, decodeRtdFlags, {}});
        e.push_back(T{QStringLiteral("Кан.%1 ADC код").arg(n),         quint16(b + 7), RegEntry::Input, false, nullptr, {}});
        e.push_back(T{QStringLiteral("Кан.%1 диапазон").arg(n),        quint16(b + 8), RegEntry::Input, false, decodeRtdRange, {}});
    }

    // Global input registers.
    e.push_back(T{QStringLiteral("Версия FW (major)"), 120, RegEntry::Input, false, nullptr, {}});
    e.push_back(T{QStringLiteral("Версия FW (minor)"), 121, RegEntry::Input, false, nullptr, {}});
    e.push_back(T{QStringLiteral("Uptime, с (low)"),  122, RegEntry::Input, false, nullptr, {}});
    e.push_back(T{QStringLiteral("Uptime, с (high)"), 123, RegEntry::Input, false, nullptr, {}});
    e.push_back(T{QStringLiteral("Module ID"),        125, RegEntry::Input, false, decodeModuleId, {}});
    e.push_back(T{QStringLiteral("Температура чипа"),  126, RegEntry::Input, false, decodeDeciCelsius, {}});
    e.push_back(T{QStringLiteral("Блокировка калибровки"), 127, RegEntry::Input, false, decodeCalLock, {}});

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
    e.push_back(T{QStringLiteral("Использовать DHCP"), 116, RegEntry::Holding, true, decodeBool01, QStringLiteral("0/1")});
    e.push_back(T{QStringLiteral("Сохранить (trigger)"),      117, RegEntry::Holding, true, nullptr, QStringLiteral("0xA5A5")});
    e.push_back(T{QStringLiteral("Перезагрузка (trigger)"),   118, RegEntry::Holding, true, nullptr, QStringLiteral("0xB00B / 0xB007")});
    e.push_back(T{QStringLiteral("Сброс к заводским (trig.)"), 119, RegEntry::Holding, true, nullptr, QStringLiteral("0xDEAD")});
    e.push_back(T{QStringLiteral("Температура чипа (HR)"), 130, RegEntry::Holding, false, decodeDeciCelsius, {}});
    e.push_back(T{QStringLiteral("Калибровка: COMMIT"), 131, RegEntry::Holding, true, nullptr, QStringLiteral("0xCA00|slot")});
    e.push_back(T{QStringLiteral("Калибровка: ERASE ARM"), 132, RegEntry::Holding, true, nullptr, QStringLiteral("0xC1A5")});

    // Per-channel configuration (holding, base 500 + ch*10).
    for (int ch = 0; ch < 4; ++ch) {
        const quint16 b = quint16(500 + ch * 10);
        const int n = ch + 1;
        e.push_back(T{QStringLiteral("Кан.%1 включён").arg(n),        quint16(b + 0), RegEntry::Holding, true, decodeBool01, QStringLiteral("0/1")});
        e.push_back(T{QStringLiteral("Кан.%1 тип датчика").arg(n),    quint16(b + 1), RegEntry::Holding, true, decodeRtdType, QStringLiteral("0..20")});
        e.push_back(T{QStringLiteral("Кан.%1 режим alpha").arg(n),    quint16(b + 2), RegEntry::Holding, true, decodeAlphaMode, QStringLiteral("0/1")});
        e.push_back(T{QStringLiteral("Кан.%1 custom W100 ×10000").arg(n), quint16(b + 3), RegEntry::Holding, true, nullptr, {}});
        e.push_back(T{QStringLiteral("Кан.%1 диапазон (override)").arg(n), quint16(b + 4), RegEntry::Holding, true, decodeCalRange, QStringLiteral("0=auto 1=low 2=high")});
    }

    // Per-channel calibration coefficients (holding float32, base 540 + ch*8).
    for (int ch = 0; ch < 4; ++ch) {
        const quint16 b = quint16(540 + ch * 8);
        const int n = ch + 1;
        e.push_back(T{QStringLiteral("Кан.%1 gain low").arg(n),   quint16(b + 0), RegEntry::Holding, true, nullptr, QStringLiteral("float"), RegEntry::F32});
        e.push_back(T{QStringLiteral("Кан.%1 offset low").arg(n), quint16(b + 2), RegEntry::Holding, true, nullptr, QStringLiteral("float"), RegEntry::F32});
        e.push_back(T{QStringLiteral("Кан.%1 gain high").arg(n),  quint16(b + 4), RegEntry::Holding, true, nullptr, QStringLiteral("float"), RegEntry::F32});
        e.push_back(T{QStringLiteral("Кан.%1 offset high").arg(n), quint16(b + 6), RegEntry::Holding, true, nullptr, QStringLiteral("float"), RegEntry::F32});
    }

    // Nominal reference resistors (holding float32).
    e.push_back(T{QStringLiteral("RREF low, Ом"),  580, RegEntry::Holding, true, nullptr, QStringLiteral("float"), RegEntry::F32});
    e.push_back(T{QStringLiteral("RREF high, Ом"), 582, RegEntry::Holding, true, nullptr, QStringLiteral("float"), RegEntry::F32});

    return e;
}

} // namespace

// ---- Public API ------------------------------------------------------------
QVector<QString> mapNames()
{
    return {
        QStringLiteral("Свободная (произвольные адреса)"),
        QStringLiteral("12DI"),
        QStringLiteral("12DO"),
        QStringLiteral("4RTD"),
    };
}

MapId mapIdForIndex(int index)
{
    switch (index) {
    case 1:  return MapId::M12DI;
    case 2:  return MapId::M12DO;
    case 3:  return MapId::M4RTD;
    default: return MapId::Free;
    }
}

QVector<RegEntry> entriesFor(MapId id)
{
    switch (id) {
    case MapId::M12DI: return build12DI();
    case MapId::M12DO: return build12DO();
    case MapId::M4RTD: return build4RTD();
    case MapId::Free:  break;
    }
    return {};
}

bool isStub(MapId)
{
    return false; // all known module maps are now implemented
}

} // namespace maps
