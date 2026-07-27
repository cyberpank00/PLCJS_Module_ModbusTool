#ifndef CRC32_H
#define CRC32_H

#include <QByteArray>
#include <cstdint>

// IEEE 802.3 / zlib CRC32 (reflected, poly 0xEDB88320, init 0xFFFFFFFF,
// final XOR 0xFFFFFFFF). Matches crc32_calc() in the bootloader and the
// crc32() implementation in tools/fw_update.mjs.
uint32_t crc32_ieee(const QByteArray &data);

#endif // CRC32_H
