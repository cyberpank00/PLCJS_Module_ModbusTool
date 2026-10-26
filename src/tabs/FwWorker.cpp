#include "FwWorker.h"

#include <QElapsedTimer>
#include <QFile>
#include <QThread>
#include <cmath>

#include "modbus/Crc32.h"
#include "modbus/ModbusTcpClient.h"
#include "protocol/Pdp.h"

FwWorker::FwWorker(QObject *parent)
    : QObject(parent)
{
}

bool FwWorker::verifyTarget(ModbusTcpClient &c, const QString &mac, boot::Status *out)
{
    QVector<quint16> regs;
    if (!c.readInputRegisters(0, boot::IR_COUNT_WITH_MAC, regs)) {
        emit logMessage(QStringLiteral("Не удалось проверить MAC загрузчика: %1").arg(c.lastError()));
        return false;
    }
    const boot::Status status = boot::parseStatus(regs);
    const QString error = boot::identityError(status, mac);
    if (!error.isEmpty()) {
        emit logMessage(error);
        return false;
    }
    if (out) *out = status;
    emit statusUpdated(status);
    return true;
}

int FwWorker::probeTarget(const QString &ip, quint16 port, const QString &mac)
{
    ModbusTcpClient c(1000);
    if (!c.connectToServer(ip, port, boot::kDefaultUnitId))
        return 0;
    return verifyTarget(c, mac) ? 1 : -1;
}

QString FwWorker::resolveBootIp(const QString &appIp, const QString &desiredIp, quint16 port, QString &mac)
{
    if (!mac.trimmed().isEmpty() && boot::normalizeMac(mac).isEmpty()) {
        emit logMessage(QStringLiteral("Некорректный MAC выбранного модуля."));
        return {};
    }
    mac = boot::normalizeMac(mac);
    // Broadcast source NIC: prefer the requested bootloader subnet.
    QString nic = pdp::nicForPeer(desiredIp);
    if (nic.isEmpty()) nic = pdp::nicForPeer(appIp);

    emit logMessage(QStringLiteral("  Поиск выбранного модуля по PDP (UDP/%1)...").arg(pdp::kPort));
    pdp::Device dev;
    QString error;
    if (!pdp::findBootloader(nic, mac, 20000, dev, &error)) {
        emit logMessage(error);
        return QString();
    }
    mac = dev.macStr;
    emit targetMacResolved(mac);
    emit logMessage(QStringLiteral("  Найден загрузчик %1 на %2").arg(dev.macStr, dev.ip));

    // 2) Directly reachable from this host (same subnet)?
    const int direct = probeTarget(dev.ip, port, mac);
    if (direct == 1) return dev.ip;
    if (direct < 0 || dev.ip == desiredIp) return {};

    // 3) Different subnet (link-local bootloader): reassign it live to the
    //    desired IP via a discovery SET_NET, then wait for it there.
    const QStringList o = desiredIp.split('.');
    const QString gw = QStringLiteral("%1.%2.%3.1").arg(o.value(0), o.value(1), o.value(2));
    for (const pdp::Device &peer : pdp::discover(nic)) {
        if (peer.ip == desiredIp && peer.macStr != mac) {
            emit logMessage(QStringLiteral("Адрес %1 занят другим модулем.").arg(desiredIp));
            return {};
        }
    }
    const int atDesired = probeTarget(desiredIp, port, mac);
    if (atDesired == 1) return desiredIp;
    if (atDesired < 0) return {};
    emit logMessage(QStringLiteral("  %1 недоступен напрямую; назначаю %2 через SET_NET...")
                        .arg(dev.ip, desiredIp));
    pdp::setNetStatic(nic, dev.macStr, desiredIp, QStringLiteral("255.255.255.0"), gw);
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 8000) {
        const int result = probeTarget(desiredIp, port, mac);
        if (result == 1) return desiredIp;
        if (result < 0) return {};
        QThread::msleep(400);
    }
    return QString();
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

bool FwWorker::waitForBootloaderReady(const QString &bootIp, quint16 port, int timeoutMs, const QString &mac)
{
    QElapsedTimer t;
    t.start();
    QString lastNotice;
    while (t.elapsed() < timeoutMs) {
        ModbusTcpClient c(1200);
        if (c.connectToServer(bootIp, port, boot::kDefaultUnitId)) {
            boot::Status s;
            if (verifyTarget(c, mac, &s)) {
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
            // The device may have jumped, or the connection may have failed.
            return InstallOutcome::Disconnected;
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
    // A timeout alone does not establish the outcome of installation.
    return InstallOutcome::Disconnected;
}

bool FwWorker::confirmAppRunning(const QString &appIp, quint16 port, int timeoutMs,
                                 const QString &mac, quint32 productId, quint32 fwVersion)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs) {
        const QList<pdp::Device> devices = pdp::discover(pdp::nicForPeer(appIp));
        bool matched = false, conflict = false;
        for (const pdp::Device &d : devices) {
            if (d.ip != appIp) continue;
            if (d.macStr != mac) conflict = true;
            else matched = !d.inBoot && d.productId == productId && d.fw == fwVersion;
        }
        if (conflict) return false;
        if (!matched) continue;
        ModbusTcpClient c(1200);
        if (c.connectToServer(appIp, port, boot::kDefaultUnitId)) {
            // Application map: IR 120 = fw major, 121 = fw minor, 125 = module id.
            QVector<quint16> regs;
            if (c.readInputRegisters(120, 6, regs) && regs.size() >= 6 &&
                regs[0] == ((fwVersion >> 8) & 255u) && regs[1] == (fwVersion & 255u)) {
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
    boot::Status s = boot::parseStatus(regs);
    if (s.version >= 0x0108u && c.readInputRegisters(0, boot::IR_COUNT_WITH_MAC, regs))
        s = boot::parseStatus(regs);
    emit statusUpdated(s);
    if (s.magic != boot::kBootloaderMagic) {
        emit logMessage(QStringLiteral("ВНИМАНИЕ: неожиданный magic 0x%1")
                            .arg(s.magic, 8, 16, QChar('0')));
    }
    emit operationFinished(true, QStringLiteral("Статус прочитан"));
}

void FwWorker::enterBootloader(const QString &appIp, const QString &bootIp, quint16 port,
                               const QString &expectedMac)
{
    const QString discovered = pdp::macForIp(pdp::nicForPeer(appIp), appIp, 3000);
    QString mac = boot::normalizeMac(expectedMac);
    if (discovered.isEmpty() || (!expectedMac.trimmed().isEmpty() && mac != discovered)) {
        emit operationFinished(false, QStringLiteral("MAC/IP приложения не подтверждены через PDP."));
        return;
    }
    mac = discovered;
    emit targetMacResolved(mac);
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

    emit logMessage(QStringLiteral("Поиск загрузчика (по умолчанию link-local, поиск по MAC)..."));
    const QString resolved = resolveBootIp(appIp, bootIp, port, mac);
    if (resolved.isEmpty()) {
        emit operationFinished(false, QStringLiteral("Загрузчик не найден (таймаут)"));
        return;
    }
    emit bootIpResolved(resolved);
    if (waitForBootloaderReady(resolved, port, 20000, mac)) {
        emit operationFinished(true, QStringLiteral("Модуль в режиме загрузчика (%1)").arg(resolved));
    } else {
        emit operationFinished(false, QStringLiteral("Загрузчик не готов (таймаут)"));
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

    // Identity is taken from the image's own fw_header_t — never hardcoded —
    // so a single tool build flashes every module variant. The bootloader
    // rejects the image if this identity does not match its own.
    const boot::FwHeader hdr = boot::parseFwHeader(fw);
    quint32 fwVersion = p.fwVersion;
    quint32 productId = p.productId;
    quint16 hwRev     = p.hwRev;
    if (hdr.valid) {
        fwVersion = hdr.fwVersion;
        productId = hdr.productId;
        hwRev     = hdr.hwRev;
    } else {
        emit operationFinished(false, QStringLiteral("В образе нет корректного заголовка прошивки."));
        return;
    }

    emit logMessage(QStringLiteral("Файл:   %1").arg(p.filePath));
    emit logMessage(QStringLiteral("Размер: %1 байт").arg(imageSize));
    emit logMessage(QStringLiteral("CRC32:  0x%1").arg(imageCrc, 8, 16, QChar('0')));
    emit logMessage(QStringLiteral("Блоков: %1 по %2 Б").arg(blockCount).arg(blockSize));
    if (hdr.valid) {
        emit logMessage(QStringLiteral("Образ:  Product ID 0x%1  hw %2.%3  fw %4.%5")
            .arg(productId, 8, 16, QChar('0'))
            .arg((hwRev >> 8) & 0xFF, 2, 10, QChar('0')).arg(hwRev & 0xFF, 2, 10, QChar('0'))
            .arg((fwVersion >> 8) & 0xFF, 2, 10, QChar('0')).arg(fwVersion & 0xFF, 2, 10, QChar('0')));
    }

    if (imageSize > boot::kStagingFlashSize) {
        emit operationFinished(false, QStringLiteral("Образ больше staging (%1 > %2)")
                                          .arg(imageSize).arg(boot::kStagingFlashSize));
        return;
    }

    // Always resolve the selected MAC, including when the IP already answers.
    QString mac = p.mac;
    const QString bootIp = resolveBootIp(p.appIp, p.bootIp, p.port, mac);
    if (bootIp.isEmpty()) {
        emit operationFinished(false, QStringLiteral("Выбранный загрузчик не найден или MAC не подтверждён."));
        return;
    }
    emit bootIpResolved(bootIp);

    ModbusTcpClient c(15000);
    if (!c.connectToServer(bootIp, p.port, boot::kDefaultUnitId)) {
        emit operationFinished(false, c.lastError());
        return;
    }

    boot::Status target;
    if (!verifyTarget(c, mac, &target)) {
        emit operationFinished(false, QStringLiteral("Обновление остановлено: MAC не подтверждён."));
        return;
    }
    if (target.productId != productId || (target.hwRev >> 16) != (hwRev >> 8)) {
        emit operationFinished(false, QStringLiteral("Product ID или аппаратная ревизия образа не подходят модулю."));
        return;
    }
    if (target.bootState != 2 && target.bootState != 4 && target.bootState != 5 && target.bootState != 9) {
        emit operationFinished(false, QStringLiteral("Загрузчик занят: %1.").arg(boot::bootStateName(target.bootState)));
        return;
    }

    // ---- Step 1: BEGIN_UPDATE --------------------------------------------
    emit logMessage(QStringLiteral("Шаг 1/4  BEGIN_UPDATE (стирание staging)..."));
    QVector<quint16> params = {
        quint16((imageSize >> 16) & 0xFFFF), quint16(imageSize & 0xFFFF),
        quint16((imageCrc >> 16) & 0xFFFF),  quint16(imageCrc & 0xFFFF),
        quint16((fwVersion >> 16) & 0xFFFF), quint16(fwVersion & 0xFFFF),
        quint16((productId >> 16) & 0xFFFF), quint16(productId & 0xFFFF),
        hwRev,
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
    // The bootloader briefly reports OK before jumping. A lost connection
    // requires confirmation from the selected module's application.
    const InstallOutcome outcome = waitForInstall(c, 30000);
    if (outcome == InstallOutcome::Failed) {
        emit operationFinished(false, QStringLiteral("INSTALL не удался"));
        return;
    }
    c.close();
    emit logMessage(outcome == InstallOutcome::Installed
                        ? QStringLiteral("  Установка подтверждена, перезагрузка в приложение...")
                        : QStringLiteral("  Связь прервана. Проверяю результат на выбранном модуле..."));

    // Confirm the freshly installed application is back online.
    emit logMessage(QStringLiteral("Проверка запуска приложения на %1...").arg(p.appIp));
    if (confirmAppRunning(p.appIp, p.port, 15000, mac, productId, fwVersion)) {
        emit operationFinished(true, QStringLiteral("Прошивка установлена, приложение запущено"));
    } else if (outcome == InstallOutcome::Installed) {
        emit operationFinished(true, QStringLiteral(
            "Загрузчик подтвердил установку. Запуск приложения не подтверждён — "
            "проверьте IP/сеть вручную."));
    } else {
        emit operationFinished(false, QStringLiteral(
            "Результат установки не подтверждён. Проверьте состояние выбранного модуля."));
    }
}

void FwWorker::abort(const QString &bootIp, quint16 port, const QString &expectedMac)
{
    QString mac = expectedMac;
    const QString ip = resolveBootIp({}, bootIp, port, mac);
    if (ip.isEmpty()) {
        emit operationFinished(false, QStringLiteral("MAC выбранного загрузчика не подтверждён."));
        return;
    }
    emit bootIpResolved(ip);
    ModbusTcpClient c(15000);
    if (!c.connectToServer(ip, port, boot::kDefaultUnitId)) {
        emit operationFinished(false, c.lastError());
        return;
    }
    if (!verifyTarget(c, mac)) {
        emit operationFinished(false, QStringLiteral("Отмена остановлена: MAC не подтверждён."));
        return;
    }
    if (!c.writeSingleRegister(boot::HR_CMD, boot::CMD_ABORT_UPDATE)
        || !waitForStatus(c, boot::CMD_STATUS_OK, 5000)) {
        emit operationFinished(false, c.lastError());
        return;
    }
    emit operationFinished(true, QStringLiteral("Сессия обновления прервана"));
}

void FwWorker::reboot(const QString &bootIp, quint16 port, const QString &expectedMac)
{
    QString mac = expectedMac;
    const QString ip = resolveBootIp({}, bootIp, port, mac);
    if (ip.isEmpty()) {
        emit operationFinished(false, QStringLiteral("MAC выбранного загрузчика не подтверждён."));
        return;
    }
    emit bootIpResolved(ip);
    ModbusTcpClient c(3000);
    if (!c.connectToServer(ip, port, boot::kDefaultUnitId)) {
        emit operationFinished(false, c.lastError());
        return;
    }
    if (!verifyTarget(c, mac)) {
        emit operationFinished(false, QStringLiteral("REBOOT остановлен: MAC не подтверждён."));
        return;
    }
    // A reset can close the connection before its acknowledgement arrives.
    if (c.writeSingleRegister(boot::HR_CMD, boot::CMD_REBOOT))
        emit operationFinished(true, QStringLiteral("Команда REBOOT принята."));
    else
        emit operationFinished(false, QStringLiteral("REBOOT не подтверждён: %1").arg(c.lastError()));
}
