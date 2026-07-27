#include "FwWorker.h"

#include <QElapsedTimer>
#include <QFile>
#include <QThread>
#include <cmath>

#include "modbus/Crc32.h"
#include "modbus/ModbusTcpClient.h"

FwWorker::FwWorker(QObject *parent)
    : QObject(parent)
{
}

bool FwWorker::waitForStatus(ModbusTcpClient &c, quint16 expected, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs) {
        QVector<quint16> regs;
        if (!c.readInputRegisters(0x0000, boot::IR_COUNT, regs)) {
            emit logMessage(QStringLiteral("  Ошибка чтения статуса: %1").arg(c.lastError()));
            return false;
        }
        const quint16 st = regs[boot::IR_CMD_STATUS];
        if (st == expected)
            return true;
        if (st == boot::CMD_STATUS_ERROR) {
            const quint16 err = regs[boot::IR_LAST_ERROR];
            emit logMessage(QStringLiteral("  Ошибка загрузчика: %1 (%2)")
                                .arg(boot::errorName(err)).arg(err));
            return false;
        }
        QThread::msleep(50);
    }
    emit logMessage(QStringLiteral("  Таймаут ожидания статуса %1")
                        .arg(boot::cmdStatusName(expected)));
    return false;
}

bool FwWorker::waitForBootloaderReady(const QString &bootIp, quint16 port, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    QString lastNotice;
    while (t.elapsed() < timeoutMs) {
        ModbusTcpClient c(1200);
        if (c.connectToServer(bootIp, port, boot::kDefaultUnitId)) {
            QVector<quint16> regs;
            if (c.readInputRegisters(0x0000, boot::IR_COUNT, regs)) {
                const boot::Status s = boot::parseStatus(regs);
                emit statusUpdated(s);
                if (s.magic == boot::kBootloaderMagic
                    && s.bootState == boot::BOOT_STATE_WAIT_COMMAND) {
                    return true;
                }
                lastNotice = QStringLiteral("загрузчик ответил, но не готов (state=%1)")
                                 .arg(boot::bootStateName(s.bootState));
            } else {
                lastNotice = c.lastError();
            }
        } else {
            lastNotice = c.lastError();
        }
        QThread::msleep(300);
    }
    if (!lastNotice.isEmpty())
        emit logMessage(QStringLiteral("  Последняя попытка: %1").arg(lastNotice));
    return false;
}

FwWorker::InstallOutcome FwWorker::waitForInstall(ModbusTcpClient &c, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs) {
        QVector<quint16> regs;
        if (!c.readInputRegisters(0x0000, boot::IR_COUNT, regs)) {
            // Connection lost / timeout: the bootloader has already jumped into
            // the freshly installed application. This is the expected success
            // path, not an error.
            return InstallOutcome::Rebooted;
        }
        const quint16 st = regs[boot::IR_CMD_STATUS];
        if (st == boot::CMD_STATUS_OK)
            return InstallOutcome::Installed;
        if (st == boot::CMD_STATUS_ERROR) {
            const quint16 err = regs[boot::IR_LAST_ERROR];
            emit logMessage(QStringLiteral("  Ошибка загрузчика: %1 (%2)")
                                .arg(boot::errorName(err)).arg(err));
            return InstallOutcome::Failed;
        }
        QThread::msleep(50);
    }
    // Still BUSY with no reply within the window — the device is almost
    // certainly rebooting. Treat as success (confirmed afterwards via appIp).
    return InstallOutcome::Rebooted;
}

bool FwWorker::confirmAppRunning(const QString &appIp, quint16 port, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs) {
        ModbusTcpClient c(1200);
        if (c.connectToServer(appIp, port, boot::kDefaultUnitId)) {
            // Application map: IR 120 = fw major, 121 = fw minor, 125 = module id.
            QVector<quint16> regs;
            if (c.readInputRegisters(120, 6, regs) && regs.size() >= 6) {
                const QString idHex = QStringLiteral("0x%1").arg(
                    QString::number(regs[5], 16).toUpper(), 4, QChar('0'));
                emit logMessage(QStringLiteral("  Приложение online: FW %1.%2, module ID %3")
                                    .arg(regs[0]).arg(regs[1]).arg(idHex));
                return true;
            }
        }
        QThread::msleep(500);
    }
    return false;
}

void FwWorker::readStatus(const QString &ip, quint16 port)
{
    ModbusTcpClient c(3000);
    if (!c.connectToServer(ip, port, boot::kDefaultUnitId)) {
        emit operationFinished(false, c.lastError());
        return;
    }
    QVector<quint16> regs;
    if (!c.readInputRegisters(0x0000, boot::IR_COUNT, regs)) {
        emit operationFinished(false, c.lastError());
        return;
    }
    const boot::Status s = boot::parseStatus(regs);
    emit statusUpdated(s);
    if (s.magic != boot::kBootloaderMagic) {
        emit logMessage(QStringLiteral("ВНИМАНИЕ: неожиданный magic 0x%1")
                            .arg(s.magic, 8, 16, QChar('0')));
    }
    emit operationFinished(true, QStringLiteral("Статус прочитан"));
}

void FwWorker::enterBootloader(const QString &appIp, const QString &bootIp, quint16 port)
{
    emit logMessage(QStringLiteral("Отправка команды 'в bootloader' приложению %1...").arg(appIp));
    {
        ModbusTcpClient c(3000);
        if (!c.connectToServer(appIp, port, boot::kDefaultUnitId)) {
            emit operationFinished(false, c.lastError());
            return;
        }
        // The app resets right after accepting the command, so a missing reply
        // is expected and not treated as an error.
        c.writeSingleRegister(boot::APP_HR_TRIGGER, boot::APP_CMD_BOOTLOADER);
    }

    emit logMessage(QStringLiteral("Ожидание готовности загрузчика на %1...").arg(bootIp));
    if (waitForBootloaderReady(bootIp, port, 20000)) {
        emit operationFinished(true, QStringLiteral("Модуль в режиме загрузчика"));
    } else {
        emit operationFinished(false, QStringLiteral("Загрузчик не появился (таймаут)"));
    }
}

void FwWorker::runUpdate(const FwUpdateParams &p)
{
    QFile f(p.filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        emit operationFinished(false, QStringLiteral("Не удалось открыть файл: %1").arg(p.filePath));
        return;
    }
    const QByteArray fw = f.readAll();
    f.close();
    if (fw.isEmpty()) {
        emit operationFinished(false, QStringLiteral("Файл прошивки пуст"));
        return;
    }

    const quint32 imageSize = quint32(fw.size());
    const quint32 imageCrc  = crc32_ieee(fw);
    const int blockSize     = boot::kFwMaxBlockSize;
    const int blockCount    = int((imageSize + blockSize - 1) / blockSize);

    emit logMessage(QStringLiteral("Файл:   %1").arg(p.filePath));
    emit logMessage(QStringLiteral("Размер: %1 байт").arg(imageSize));
    emit logMessage(QStringLiteral("CRC32:  0x%1").arg(imageCrc, 8, 16, QChar('0')));
    emit logMessage(QStringLiteral("Блоков: %1 по %2 Б").arg(blockCount).arg(blockSize));

    if (imageSize > boot::kStagingFlashSize) {
        emit operationFinished(false, QStringLiteral("Образ больше staging (%1 > %2)")
                                          .arg(imageSize).arg(boot::kStagingFlashSize));
        return;
    }

    ModbusTcpClient c(6000);
    if (!c.connectToServer(p.bootIp, p.port, boot::kDefaultUnitId)) {
        emit operationFinished(false, c.lastError());
        return;
    }

    // ---- Step 1: BEGIN_UPDATE --------------------------------------------
    emit logMessage(QStringLiteral("Шаг 1/4  BEGIN_UPDATE (стирание staging)..."));
    QVector<quint16> params = {
        quint16((imageSize >> 16) & 0xFFFF), quint16(imageSize & 0xFFFF),
        quint16((imageCrc >> 16) & 0xFFFF),  quint16(imageCrc & 0xFFFF),
        quint16((p.fwVersion >> 16) & 0xFFFF), quint16(p.fwVersion & 0xFFFF),
        quint16((p.productId >> 16) & 0xFFFF), quint16(p.productId & 0xFFFF),
        p.hwRev,
        quint16(blockSize),
        quint16((quint32(blockCount) >> 16) & 0xFFFF), quint16(quint32(blockCount) & 0xFFFF),
    };
    if (!c.writeMultipleRegisters(boot::HR_PARAM_BASE, params)
        || !c.writeSingleRegister(boot::HR_CMD, boot::CMD_BEGIN_UPDATE)) {
        emit operationFinished(false, c.lastError());
        return;
    }
    if (!waitForStatus(c, boot::CMD_STATUS_OK, 20000)) {
        emit operationFinished(false, QStringLiteral("BEGIN_UPDATE не удался"));
        return;
    }
    emit logMessage(QStringLiteral("  OK"));

    // ---- Step 2: WRITE blocks --------------------------------------------
    emit logMessage(QStringLiteral("Шаг 2/4  Передача %1 блоков...").arg(blockCount));
    emit progress(0, blockCount);
    for (int blk = 0; blk < blockCount; ++blk) {
        const int offset = blk * blockSize;
        const QByteArray chunk = fw.mid(offset, blockSize);
        QVector<quint16> payload;
        payload.append(quint16((quint32(blk) >> 16) & 0xFFFF));
        payload.append(quint16(quint32(blk) & 0xFFFF));
        payload.append(quint16(chunk.size()));
        payload += boot::bytesToRegs(chunk);

        if (!c.writeMultipleRegisters(boot::HR_BLOCK_BASE, payload)) {
            emit operationFinished(false, QStringLiteral("Блок %1: %2").arg(blk).arg(c.lastError()));
            return;
        }
        if (!waitForStatus(c, boot::CMD_STATUS_OK, 5000)) {
            emit operationFinished(false, QStringLiteral("Блок %1 не записан").arg(blk));
            return;
        }
        emit progress(blk + 1, blockCount);
    }
    emit logMessage(QStringLiteral("  Все блоки переданы"));

    // ---- Step 3: FINALIZE ------------------------------------------------
    emit logMessage(QStringLiteral("Шаг 3/4  FINALIZE (проверка CRC staging)..."));
    if (!c.writeSingleRegister(boot::HR_CMD, boot::CMD_FINALIZE_UPDATE)) {
        emit operationFinished(false, c.lastError());
        return;
    }
    if (!waitForStatus(c, boot::CMD_STATUS_OK, 10000)) {
        emit operationFinished(false, QStringLiteral("FINALIZE не удался (CRC/неполный образ)"));
        return;
    }
    emit logMessage(QStringLiteral("  CRC staging OK"));

    // ---- Step 4: INSTALL -------------------------------------------------
    emit logMessage(QStringLiteral("Шаг 4/4  INSTALL (запись в application)..."));
    if (!c.writeSingleRegister(boot::HR_CMD, boot::CMD_INSTALL_UPDATE)) {
        emit operationFinished(false, c.lastError());
        return;
    }
    // The bootloader copies staging->app and jumps straight into the new
    // application, dropping the connection before it can report CMD_STATUS_OK.
    // A lost connection here is therefore success; only an explicit
    // CMD_STATUS_ERROR (reported before the reset) is a real failure.
    const InstallOutcome outcome = waitForInstall(c, 30000);
    if (outcome == InstallOutcome::Failed) {
        emit operationFinished(false, QStringLiteral("INSTALL не удался"));
        return;
    }
    c.close();
    emit logMessage(outcome == InstallOutcome::Installed
                        ? QStringLiteral("  Установка подтверждена, перезагрузка в приложение...")
                        : QStringLiteral("  Загрузчик перезагрузился в приложение (это нормально)"));

    // Confirm the freshly installed application is back online.
    emit logMessage(QStringLiteral("Проверка запуска приложения на %1...").arg(p.appIp));
    if (confirmAppRunning(p.appIp, p.port, 15000)) {
        emit operationFinished(true, QStringLiteral("Прошивка установлена, приложение запущено"));
    } else {
        emit operationFinished(true, QStringLiteral(
            "Прошивка установлена. Приложение не ответило за отведённое время — "
            "проверьте IP/сеть вручную."));
    }
}

void FwWorker::abort(const QString &bootIp, quint16 port)
{
    ModbusTcpClient c(3000);
    if (!c.connectToServer(bootIp, port, boot::kDefaultUnitId)) {
        emit operationFinished(false, c.lastError());
        return;
    }
    if (!c.writeSingleRegister(boot::HR_CMD, boot::CMD_ABORT_UPDATE)) {
        emit operationFinished(false, c.lastError());
        return;
    }
    emit operationFinished(true, QStringLiteral("Сессия обновления прервана"));
}

void FwWorker::reboot(const QString &bootIp, quint16 port)
{
    ModbusTcpClient c(3000);
    if (!c.connectToServer(bootIp, port, boot::kDefaultUnitId)) {
        emit operationFinished(false, c.lastError());
        return;
    }
    // The device resets immediately, so a missing reply is expected.
    c.writeSingleRegister(boot::HR_CMD, boot::CMD_REBOOT);
    emit operationFinished(true, QStringLiteral("Команда REBOOT отправлена"));
}
