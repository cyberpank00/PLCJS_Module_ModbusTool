#ifndef FWWORKER_H
#define FWWORKER_H

#include <QObject>
#include <QString>

#include "protocol/BootloaderProtocol.h"

// Parameters for a firmware update run.
struct FwUpdateParams {
    QString bootIp;
    QString appIp;
    QString mac;
    quint16 port = boot::kDefaultPort;
    QString filePath;
    // These three are compile-time constants defined in the bootloader.
    // The tool must never let the user override them:
    //   fwVersion — bootloader stores 0; actual version is read from fw_header_t
    //   productId — must match PRODUCT_ID_DEFAULT baked into the bootloader binary
    //   hwRev     — (major<<8)|minor, must match HW_REVISION_DEFAULT major byte
    quint32 fwVersion = 0u;
    quint32 productId = boot::kProductIdDefault;
    quint16 hwRev     = boot::kHwRevisionDefault;
};

// Runs blocking Modbus operations. Lives in its own QThread; the GUI talks to
// it exclusively through queued signals/slots so the UI never blocks.
class FwWorker : public QObject
{
    Q_OBJECT
public:
    explicit FwWorker(QObject *parent = nullptr);

public slots:
    void readStatus(const QString &ip, quint16 port);
    void enterBootloader(const QString &appIp, const QString &bootIp, quint16 port, const QString &mac);
    void runUpdate(const FwUpdateParams &p);
    void abort(const QString &bootIp, quint16 port, const QString &mac);
    void reboot(const QString &bootIp, quint16 port, const QString &mac);

signals:
    void logMessage(const QString &text);
    void progress(int done, int total);
    void statusUpdated(const boot::Status &status);
    void operationFinished(bool ok, const QString &summary);
    // Emitted when the bootloader's IP is discovered/reassigned so the UI can
    // update its "boot IP" field for the follow-up flash.
    void bootIpResolved(const QString &ip);
    void targetMacResolved(const QString &mac);

private:
    // Poll IR_CMD_STATUS until it equals `expected` or hits ERROR/timeout.
    bool waitForStatus(class ModbusTcpClient &c, quint16 expected, int timeoutMs);
    // Wait until the bootloader answers at bootIp and is in WAIT_COMMAND.
    bool waitForBootloaderReady(const QString &bootIp, quint16 port, int timeoutMs, const QString &mac);
    // 1 = verified, 0 = no connection, -1 = connected but identity unverified.
    int probeTarget(const QString &ip, quint16 port, const QString &mac);
    bool verifyTarget(class ModbusTcpClient &c, const QString &mac, boot::Status *out = nullptr);
    // Resolve the bootloader's reachable IP: the bootloader now defaults to a
    // link-local address, so if `desiredIp` is silent, discover it by MAC (via
    // PDP) and, when it sits on a different subnet, reassign it live to
    // `desiredIp` with a discovery SET_NET. Returns "" on failure.
    QString resolveBootIp(const QString &appIp, const QString &desiredIp, quint16 port, QString &mac);

    // Outcome of the INSTALL command. The bootloader jumps straight into the
    // application after installing. A lost connection needs independent
    // confirmation; it is not evidence of successful installation.
    enum class InstallOutcome { Installed, Disconnected, Failed };
    InstallOutcome waitForInstall(class ModbusTcpClient &c, int timeoutMs);
    // Poll the application's Modbus map at appIp to confirm it came back up.
    bool confirmAppRunning(const QString &appIp, quint16 port, int timeoutMs,
                           const QString &mac, quint32 productId, quint32 fwVersion);
};

Q_DECLARE_METATYPE(FwUpdateParams)
Q_DECLARE_METATYPE(boot::Status)

#endif // FWWORKER_H
