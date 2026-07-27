#include "Crc32.h"

#include <array>

namespace {

std::array<uint32_t, 256> makeTable()
{
    std::array<uint32_t, 256> table{};
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k)
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        table[n] = c;
    }
    return table;
}

const std::array<uint32_t, 256> kTable = makeTable();

} // namespace

uint32_t crc32_ieee(const QByteArray &data)
{
    uint32_t crc = 0xFFFFFFFFu;
    const auto *p = reinterpret_cast<const uint8_t *>(data.constData());
    for (int i = 0; i < data.size(); ++i)
        crc = kTable[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}
