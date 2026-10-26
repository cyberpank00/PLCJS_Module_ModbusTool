#include "BootloaderProtocol.h"
#include <QRegularExpression>

namespace boot {

Status parseStatus(const QVector<quint16> &regs)
{
    Status s;
    if (regs.size() < IR_COUNT)
        return s;
    s.magic        = u32FromRegs(regs, IR_MAGIC_HI);
    s.version      = u32FromRegs(regs, IR_VERSION_HI);
    s.bootState    = regs[IR_BOOT_STATE];
    s.appValid     = regs[IR_APP_VALID];
    s.appVersion   = u32FromRegs(regs, IR_APP_VER_HI);
    s.productId    = u32FromRegs(regs, IR_PRODUCT_ID_HI);
    s.hwRev        = u32FromRegs(regs, IR_HW_REV_HI); // 32-bit: major.minor.patch
    s.lastError    = regs[IR_LAST_ERROR];
    s.blockCount   = u32FromRegs(regs, IR_BLOCK_COUNT_HI);
    s.recvBlocks   = u32FromRegs(regs, IR_RECV_BLOCKS_HI);
    s.imageSize    = u32FromRegs(regs, IR_IMAGE_SIZE_HI);
    s.imageCrc     = u32FromRegs(regs, IR_IMAGE_CRC_HI);
    s.cmdStatus    = regs[IR_CMD_STATUS];
    s.stagingValid = regs[IR_STAGING_VALID];
    s.appProductId = u32FromRegs(regs, IR_APP_PRODUCT_ID_HI);
    if (regs.size() >= IR_COUNT_WITH_MAC) {
        QStringList bytes;
        for (int i = IR_MAC; i < IR_COUNT_WITH_MAC; ++i) {
            bytes << QStringLiteral("%1").arg(regs[i] >> 8, 2, 16, QChar('0'))
                  << QStringLiteral("%1").arg(regs[i] & 255, 2, 16, QChar('0'));
        }
        s.mac = normalizeMac(bytes.join(':'));
    }
    return s;
}

QString normalizeMac(const QString &mac)
{
    const QString value = mac.trimmed().toLower().replace('-', ':');
    static const QRegularExpression pattern(QStringLiteral("^(?:[0-9a-f]{2}:){5}[0-9a-f]{2}$"));
    if (!pattern.match(value).hasMatch() || value == QStringLiteral("00:00:00:00:00:00")
        || (value.left(2).toUInt(nullptr, 16) & 1u))
        return {};
    return value;
}

QString identityError(const Status &status, const QString &expectedMac)
{
    const QString expected = normalizeMac(expectedMac);
    if (expected.isEmpty())
        return QStringLiteral("Укажите корректный MAC выбранного модуля.");
    if (status.magic != kBootloaderMagic)
        return QStringLiteral("Подключённое устройство не является загрузчиком.");
    if (status.version < 0x0108u || status.mac.isEmpty())
        return QStringLiteral("Загрузчик не поддерживает проверку MAC. Требуется BL 1.8 или новее.");
    if (status.mac != expected)
        return QStringLiteral("MAC не совпадает: выбран %1, подключён %2.").arg(expected, status.mac);
    return {};
}

FwHeader parseFwHeader(const QByteArray &image)
{
    FwHeader h;
    // fw_header_t (packed): magic(4) product_id(4) hw_revision(2) reserved0(2)
    //                       fw_version(4) ...  — little-endian on the STM32.
    if (image.size() < kFwHeaderOffset + 28)
        return h;
    const auto *p = reinterpret_cast<const quint8 *>(image.constData()) + kFwHeaderOffset;
    auto rd32 = [](const quint8 *b) {
        return quint32(b[0]) | (quint32(b[1]) << 8) | (quint32(b[2]) << 16) | (quint32(b[3]) << 24);
    };
    auto rd16 = [](const quint8 *b) { return quint16(b[0]) | (quint16(b[1]) << 8); };

    if (rd32(p) != kFwImageMagic || rd32(p + 16) != 0)
        return h; // no valid header
    h.valid     = true;
    h.productId = rd32(p + 4);
    h.hwRev     = rd16(p + 8);
    h.fwVersion = rd32(p + 12);
    return h;
}

QString bootStateName(quint16 state)
{
    switch (state) {
    case 0: return QStringLiteral("BOOT_START");
    case 1: return QStringLiteral("BOOT_CHECK_ENTRY");
    case 2: return QStringLiteral("BOOT_WAIT_COMMAND");
    case 3: return QStringLiteral("BOOT_PREPARE_UPDATE");
    case 4: return QStringLiteral("BOOT_RECEIVE_FW");
    case 5: return QStringLiteral("BOOT_VERIFY_STAGING");
    case 6: return QStringLiteral("BOOT_INSTALL_FW");
    case 7: return QStringLiteral("BOOT_VERIFY_APP");
    case 8: return QStringLiteral("BOOT_READY_TO_BOOT");
    case 9: return QStringLiteral("BOOT_ERROR");
    default: return QString::number(state);
    }
}

QString errorName(quint16 code)
{
    switch (code) {
    case 0:  return QStringLiteral("NONE");
    case 1:  return QStringLiteral("PRODUCT_MISMATCH");
    case 2:  return QStringLiteral("HW_REV_MISMATCH");
    case 3:  return QStringLiteral("IMAGE_TOO_LARGE");
    case 4:  return QStringLiteral("BLOCK_CRC");
    case 5:  return QStringLiteral("IMAGE_CRC");
    case 6:  return QStringLiteral("FLASH_ERASE");
    case 7:  return QStringLiteral("FLASH_WRITE");
    case 8:  return QStringLiteral("APP_VALIDATE");
    case 9:  return QStringLiteral("UPDATE_TIMEOUT");
    case 10: return QStringLiteral("BLOCK_INDEX");
    case 11: return QStringLiteral("BAD_PARAMS");
    default: return QString::number(code);
    }
}

QString cmdStatusName(quint16 status)
{
    switch (status) {
    case CMD_STATUS_IDLE:  return QStringLiteral("IDLE");
    case CMD_STATUS_BUSY:  return QStringLiteral("BUSY");
    case CMD_STATUS_OK:    return QStringLiteral("OK");
    case CMD_STATUS_ERROR: return QStringLiteral("ERROR");
    default: return QString::number(status);
    }
}

QVector<quint16> bytesToRegs(const QByteArray &data)
{
    QVector<quint16> regs;
    const auto *p = reinterpret_cast<const uint8_t *>(data.constData());
    for (int i = 0; i < data.size(); i += 2) {
        const quint8 hi = p[i];
        const quint8 lo = (i + 1 < data.size()) ? p[i + 1] : 0xFF;
        regs.append(quint16((quint16(hi) << 8) | lo));
    }
    return regs;
}

} // namespace boot
