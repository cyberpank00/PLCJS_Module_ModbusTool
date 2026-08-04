#ifndef PDP_H
#define PDP_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <cstdint>

// PLCJS Discovery Protocol (PDP) — UDP/20556 broadcast client helpers.
// Synchronous (blocking) API intended for use from a worker thread; mirrors
// Application/discovery/discovery.c and Tools/fw_update.mjs. Used by the
// firmware-update flow to locate a link-local bootloader by MAC and reassign
// its address, and shared with the discovery UI.
namespace pdp {

constexpr quint16 kPort = 20556;

struct Device {
    QByteArray mac;            // 6 raw bytes
    QString    macStr;         // "aa:bb:cc:dd:ee:ff"
    quint32    productId = 0;
    quint16    fw = 0;
    quint8     netMode = 0;    // 0=static, 1=dhcp, 2=link-local
    bool       inBoot = false; // true when the device is in the bootloader
    QString    ip;
    QString    mask;
    QString    name;
};

// Broadcast IDENTIFY from local `nicIp` (empty = OS default route) and collect
// every responder for `timeoutMs`.
QList<Device> discover(const QString &nicIp, int timeoutMs = 1500);

// Poll discovery until a bootloader (inBoot) matching `macStr` is found
// (empty macStr = the sole bootloader). Returns true and fills `out`.
bool findBootloader(const QString &nicIp, const QString &macStr,
                    int timeoutMs, Device &out);

// IDENTIFY whatever device currently answers at `ip`; return its MAC (or "").
QString macForIp(const QString &nicIp, const QString &ip, int timeoutMs);

// Broadcast a SET_NET (static) command to `macStr`, re-sent for reliability.
void setNetStatic(const QString &nicIp, const QString &macStr,
                  const QString &ip, const QString &mask, const QString &gw);

// Local IPv4 address on the same subnet as `peerIp` (to bind the broadcast
// socket on a multi-homed host). Empty if none matches.
QString nicForPeer(const QString &peerIp);

} // namespace pdp

#endif // PDP_H
