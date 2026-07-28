#ifndef BOOTLOADERPROTOCOL_H
#define BOOTLOADERPROTOCOL_H

#include <QByteArray>
#include <QString>
#include <QVector>
#include <cstdint>

// Register map and command constants for the STM32F407 bootloader OTA
// protocol over Modbus TCP. Values mirror tools/fw_update.mjs and the
// bootloader's fw_update_proto.h.
namespace boot {

// ---- Target defaults ----------------------------------------------------
constexpr quint16 kDefaultPort      = 502;
constexpr quint8  kDefaultUnitId    = 1;
constexpr char    kDefaultBootIp[]  = "192.168.142.99";
constexpr char    kDefaultAppIp[]   = "192.168.142.98";

constexpr quint32 kProductIdDefault = 0x12D1D4A0u;
constexpr quint16 kHwRevisionDefault = 0x0101u; // (major<<8)|minor for OTA params
constexpr quint32 kBootloaderMagic  = 0xB00710ADu;

constexpr int     kFwMaxBlockSize   = 240;          // bytes, must match flash_map.h
constexpr quint32 kStagingFlashSize = 256u * 1024u; // 256 KB

// ---- Input registers (FC04, base 0x0000) --------------------------------
// NOTE: IR_HW_REV_HI occupies two registers (32-bit, (major<<16)|(minor<<8)|patch).
//       All registers from IR_LAST_ERROR onward are shifted by +1 vs. older firmware.
enum InputReg : quint16 {
    IR_MAGIC_HI       = 0x0000,
    IR_VERSION_HI     = 0x0002,
    IR_BOOT_STATE     = 0x0004,
    IR_APP_VALID      = 0x0005,
    IR_APP_VER_HI     = 0x0006,
    IR_PRODUCT_ID_HI  = 0x0008,
    IR_HW_REV_HI      = 0x000A,  // 32-bit: 0x0A (hi word) + 0x0B (lo word)
    IR_LAST_ERROR     = 0x000C,  // was 0x0B
    IR_BLOCK_COUNT_HI = 0x000D,  // was 0x0C
    IR_RECV_BLOCKS_HI = 0x000F,  // was 0x0E
    IR_IMAGE_SIZE_HI  = 0x0011,  // was 0x10
    IR_IMAGE_CRC_HI   = 0x0013,  // was 0x12
    IR_CMD_STATUS     = 0x0015,  // was 0x14
    IR_STAGING_VALID  = 0x0016,  // was 0x15
    IR_COUNT          = 0x0017,  // was 0x16
};

// ---- Holding registers (FC03/FC16, base 0x0000) -------------------------
constexpr quint16 HR_CMD        = 0x0000;
constexpr quint16 HR_PARAM_BASE = 0x0010; // size,crc,ver,prodid,hwrev,blocksize,blockcount
constexpr quint16 HR_BLOCK_BASE = 0x0100; // block_idx(2), data_len(1), data...

// ---- Bootloader commands (written to HR_CMD) ----------------------------
enum Command : quint16 {
    CMD_BEGIN_UPDATE    = 1,
    CMD_FINALIZE_UPDATE = 2,
    CMD_INSTALL_UPDATE  = 3,
    CMD_ABORT_UPDATE    = 4,
    CMD_REBOOT          = 5,
};

enum CmdStatus : quint16 {
    CMD_STATUS_IDLE  = 0,
    CMD_STATUS_BUSY  = 1,
    CMD_STATUS_OK    = 2,
    CMD_STATUS_ERROR = 3,
};

constexpr quint16 BOOT_STATE_WAIT_COMMAND = 2;

// ---- Application control registers (on the app's Modbus map) ------------
constexpr quint16 APP_HR_TRIGGER        = 118;
constexpr quint16 APP_HR_FACTORY_RESET  = 119;
constexpr quint16 APP_CMD_REBOOT        = 0xB00B;
constexpr quint16 APP_CMD_BOOTLOADER    = 0xB007;
constexpr quint16 APP_CMD_FACTORY_RESET = 0xDEAD;

// ---- Parsed status snapshot ---------------------------------------------
struct Status {
    quint32 magic       = 0;
    quint32 version     = 0;
    quint16 bootState   = 0;
    quint16 appValid    = 0;
    quint32 appVersion  = 0;
    quint32 productId   = 0;
    quint32 hwRev       = 0; // (major<<16)|(minor<<8)|patch
    quint16 lastError   = 0;
    quint32 blockCount  = 0;
    quint32 recvBlocks  = 0;
    quint32 imageSize   = 0;
    quint32 imageCrc    = 0;
    quint16 cmdStatus   = 0;
    quint16 stagingValid = 0;
};

// Parse a Status from an IR_COUNT-length register vector (read from 0x0000).
Status parseStatus(const QVector<quint16> &regs);

// Human-readable names.
QString bootStateName(quint16 state);
QString errorName(quint16 code);
QString cmdStatusName(quint16 status);

// Pack firmware bytes into big-endian 16-bit registers, padding an odd
// trailing byte with 0xFF (mirrors bytesToRegs in fw_update.mjs).
QVector<quint16> bytesToRegs(const QByteArray &data);

// Helpers to (de)compose 32-bit values across two registers, high word first.
inline quint32 u32FromRegs(const QVector<quint16> &r, int base)
{
    if (base + 1 >= r.size()) return 0;
    return (quint32(r[base]) << 16) | quint32(r[base + 1]);
}

} // namespace boot

#endif // BOOTLOADERPROTOCOL_H
