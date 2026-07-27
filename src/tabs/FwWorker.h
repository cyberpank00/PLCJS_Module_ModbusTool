#ifndef FWWORKER_H
#define FWWORKER_H

#include <QObject>
#include <QString>

#include "protocol/BootloaderProtocol.h"

// Parameters for a firmware update run.
struct FwUpdateParams {
    QString bootIp;
    QString appIp;
    quint16 port = boot::kDefaultPort;
    QString filePath;
    quint32 fwVersion = 0x00010000u;
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
    void enterBootloader(const QString &appIp, const QString &bootIp, quint16 port);
    void runUpdate(const FwUpdateParams &p);
    void abort(const QString &bootIp, quint16 port);
    void reboot(const QString &bootIp, quint16 port);

signals:
    void logMessage(const QString &text);
    void progress(int done, int total);
    void statusUpdated(const boot::Status &status);
    void operationFinished(bool ok, const QString &summary);

private:
    // Poll IR_CMD_STATUS until it equals `expected` or hits ERROR/timeout.
    bool waitForStatus(class ModbusTcpClient &c, quint16 expected, int timeoutMs);
    // Wait until the bootloader answers at bootIp and is in WAIT_COMMAND.
    bool waitForBootloaderReady(const QString &bootIp, quint16 port, int timeoutMs);

    // Outcome of the INSTALL command. The bootloader jumps straight into the
    // application after installing, dropping the connection before it can
    // report CMD_STATUS_OK, so a lost connection here means success.
    enum class InstallOutcome { Installed, Rebooted, Failed };
    InstallOutcome waitForInstall(class ModbusTcpClient &c, int timeoutMs);
    // Poll the application's Modbus map at appIp to confirm it came back up.
    bool confirmAppRunning(const QString &appIp, quint16 port, int timeoutMs);
};

Q_DECLARE_METATYPE(FwUpdateParams)
Q_DECLARE_METATYPE(boot::Status)

#endif // FWWORKER_H
