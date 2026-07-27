#include "BootloaderProtocol.h"

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
    s.hwRev        = regs[IR_HW_REV];
    s.lastError    = regs[IR_LAST_ERROR];
    s.blockCount   = u32FromRegs(regs, IR_BLOCK_COUNT_HI);
    s.recvBlocks   = u32FromRegs(regs, IR_RECV_BLOCKS_HI);
    s.imageSize    = u32FromRegs(regs, IR_IMAGE_SIZE_HI);
    s.imageCrc     = u32FromRegs(regs, IR_IMAGE_CRC_HI);
    s.cmdStatus    = regs[IR_CMD_STATUS];
    s.stagingValid = regs[IR_STAGING_VALID];
    return s;
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
