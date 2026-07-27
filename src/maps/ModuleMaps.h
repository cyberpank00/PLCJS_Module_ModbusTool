#ifndef MODULEMAPS_H
#define MODULEMAPS_H

#include <QString>
#include <QVector>
#include <cstdint>

// Named Modbus register maps ("карты") for the PLCJS ETH I/O modules, used by
// the Онлайн tab to present labelled rows instead of raw addresses.
//
// Only the 12DI map is fully described here (its firmware repo is available and
// the module is used for live testing). 12DO and 4RTD are declared as stubs and
// will be filled in later; other variants (4AIU/4AIC/4AO) are not modelled yet.
namespace maps {

// A single register row in a module map.
struct RegEntry {
    enum Type { Input, Holding }; // FC04 (read-only) vs FC03/FC06 (read/write)
    enum Fmt  { U16, F32 };       // 1 register (uint16) vs 2 registers (float32,
                                  // IEEE-754, HIGH word first)

    QString name;      // human-readable label shown in the "Имя" column
    quint16 addr;      // Modbus register address
    Type    type;      // input or holding register
    bool    writable;  // whether a value may be written (FC06/FC16)

    // Optional decoder: turns the raw 16-bit value into a human-readable string
    // (e.g. temperature in °C, LED mode name). Only used for U16 registers;
    // nullptr means "no decoding".
    QString (*decode)(quint16 raw) = nullptr;

    // Optional hint shown as a tooltip / placeholder for the write field
    // (e.g. accepted range or magic value). May be empty.
    QString writeHint;

    // Value width / interpretation. F32 spans two consecutive registers.
    Fmt fmt = U16;
};

// Identifies which map a combo-box entry selects.
enum class MapId {
    Free,   // arbitrary addresses (classic manual mode)
    M12DI,  // 12x discrete inputs — fully described
    M12DO,  // 12x discrete outputs — stub
    M4RTD,  // 4x RTD analog inputs — stub
};

// Display names for the "Карта модуля" combo box, index-aligned with allMaps().
QVector<QString> mapNames();

// Returns the MapId for a given combo-box index.
MapId mapIdForIndex(int index);

// Returns the register entries for the given map. Free/stub maps return empty.
QVector<RegEntry> entriesFor(MapId id);

// True if the map is a not-yet-implemented stub (12DO/4RTD).
bool isStub(MapId id);

} // namespace maps

#endif // MODULEMAPS_H
