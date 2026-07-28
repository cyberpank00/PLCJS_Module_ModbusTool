#include "FwUpdateTab.h"

#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QThread>
#include <QVBoxLayout>

#include "modbus/Crc32.h"

FwUpdateTab::FwUpdateTab(QWidget *parent)
    : QWidget(parent)
{
    // ---- Target / parameters --------------------------------------------
    m_appIp = new QLineEdit(QString::fromLatin1(boot::kDefaultAppIp));
    m_bootIp = new QLineEdit(QString::fromLatin1(boot::kDefaultBootIp));
    m_port = new QSpinBox;
    m_port->setRange(1, 65535);
    m_port->setValue(boot::kDefaultPort);
    auto *paramBox = new QGroupBox(QStringLiteral("Параметры"));
    auto *paramForm = new QFormLayout(paramBox);
    paramForm->addRow(QStringLiteral("IP приложения:"), m_appIp);
    paramForm->addRow(QStringLiteral("IP загрузчика:"), m_bootIp);
    paramForm->addRow(QStringLiteral("Порт:"), m_port);

    // ---- File picker -----------------------------------------------------
    m_file = new QLineEdit;
    m_file->setReadOnly(true);
    m_selectBtn = new QPushButton(QStringLiteral("Выбрать .bin..."));
    m_fileInfo = new QLabel(QStringLiteral("Файл не выбран"));
    m_fileInfo->setWordWrap(true);
    connect(m_selectBtn, &QPushButton::clicked, this, &FwUpdateTab::onSelectFile);

    auto *fileRow = new QHBoxLayout;
    fileRow->addWidget(m_file, 1);
    fileRow->addWidget(m_selectBtn);

    auto *fileBox = new QGroupBox(QStringLiteral("Прошивка"));
    auto *fileLay = new QVBoxLayout(fileBox);
    fileLay->addLayout(fileRow);
    fileLay->addWidget(m_fileInfo);

    // ---- Action buttons --------------------------------------------------
    m_statusBtn = new QPushButton(QStringLiteral("Статус"));
    m_bootBtn = new QPushButton(QStringLiteral("В bootloader"));
    m_flashBtn = new QPushButton(QStringLiteral("Прошить"));
    m_abortBtn = new QPushButton(QStringLiteral("Abort"));
    m_rebootBtn = new QPushButton(QStringLiteral("Reboot"));
    connect(m_statusBtn, &QPushButton::clicked, this, &FwUpdateTab::onStatusClicked);
    connect(m_bootBtn, &QPushButton::clicked, this, &FwUpdateTab::onEnterBootloaderClicked);
    connect(m_flashBtn, &QPushButton::clicked, this, &FwUpdateTab::onFlashClicked);
    connect(m_abortBtn, &QPushButton::clicked, this, &FwUpdateTab::onAbortClicked);
    connect(m_rebootBtn, &QPushButton::clicked, this, &FwUpdateTab::onRebootClicked);

    auto *btnBox = new QGroupBox(QStringLiteral("Действия"));
    auto *btnLay = new QHBoxLayout(btnBox);
    btnLay->addWidget(m_statusBtn);
    btnLay->addWidget(m_bootBtn);
    btnLay->addWidget(m_flashBtn);
    btnLay->addWidget(m_abortBtn);
    btnLay->addWidget(m_rebootBtn);

    // ---- Status panel ----------------------------------------------------
    auto makeLabel = []() { return new QLabel(QStringLiteral("—")); };
    m_lBootState = makeLabel();
    m_lBootVer = makeLabel();
    m_lLastError = makeLabel();
    m_lAppValid = makeLabel();
    m_lAppVer = makeLabel();
    m_lProductId = makeLabel();
    m_lAppProductId = makeLabel();
    m_lHwRev = makeLabel();
    m_lBlocks = makeLabel();
    m_lImageSize = makeLabel();
    m_lImageCrc = makeLabel();
    m_lCmdStatus = makeLabel();
    m_lStaging = makeLabel();

    auto *statusBox = new QGroupBox(QStringLiteral("Статус загрузчика"));
    auto *sForm = new QFormLayout(statusBox);
    sForm->addRow(QStringLiteral("Boot state:"),      m_lBootState);
    sForm->addRow(QStringLiteral("Bootloader ver:"),  m_lBootVer);
    sForm->addRow(QStringLiteral("Last error:"),      m_lLastError);
    sForm->addRow(QStringLiteral("App valid:"), m_lAppValid);
    sForm->addRow(QStringLiteral("App version:"), m_lAppVer);
    sForm->addRow(QStringLiteral("Product ID (загр.):"), m_lProductId);
    sForm->addRow(QStringLiteral("Product ID (прил.):"), m_lAppProductId);
    sForm->addRow(QStringLiteral("HW revision:"), m_lHwRev);
    sForm->addRow(QStringLiteral("Blocks:"), m_lBlocks);
    sForm->addRow(QStringLiteral("Image size:"), m_lImageSize);
    sForm->addRow(QStringLiteral("Image CRC32:"), m_lImageCrc);
    sForm->addRow(QStringLiteral("Cmd status:"), m_lCmdStatus);
    sForm->addRow(QStringLiteral("Staging valid:"), m_lStaging);

    // ---- Progress + log --------------------------------------------------
    m_progress = new QProgressBar;
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);

    // ---- Layout ----------------------------------------------------------
    auto *leftCol = new QVBoxLayout;
    leftCol->addWidget(paramBox);
    leftCol->addWidget(fileBox);
    leftCol->addWidget(btnBox);
    leftCol->addWidget(m_progress);
    leftCol->addStretch();

    auto *rightCol = new QVBoxLayout;
    rightCol->addWidget(statusBox);
    rightCol->addWidget(new QLabel(QStringLiteral("Журнал:")));
    rightCol->addWidget(m_log, 1);

    auto *root = new QHBoxLayout(this);
    root->addLayout(leftCol, 0);
    root->addLayout(rightCol, 1);

    // ---- Worker thread ---------------------------------------------------
    m_thread = new QThread(this);
    m_worker = new FwWorker;
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(this, &FwUpdateTab::requestStatus, m_worker, &FwWorker::readStatus);
    connect(this, &FwUpdateTab::requestEnterBootloader, m_worker, &FwWorker::enterBootloader);
    connect(this, &FwUpdateTab::requestUpdate, m_worker, &FwWorker::runUpdate);
    connect(this, &FwUpdateTab::requestAbort, m_worker, &FwWorker::abort);
    connect(this, &FwUpdateTab::requestReboot, m_worker, &FwWorker::reboot);

    connect(m_worker, &FwWorker::logMessage, this, &FwUpdateTab::onLog);
    connect(m_worker, &FwWorker::progress, this, &FwUpdateTab::onProgress);
    connect(m_worker, &FwWorker::statusUpdated, this, &FwUpdateTab::onStatusUpdated);
    connect(m_worker, &FwWorker::operationFinished, this, &FwUpdateTab::onFinished);

    m_thread->start();
}

FwUpdateTab::~FwUpdateTab()
{
    m_thread->quit();
    m_thread->wait();
}


void FwUpdateTab::onSelectFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Выбор образа прошивки"), QString(),
        QStringLiteral("Firmware (*.bin);;Все файлы (*.*)"));
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        m_fileInfo->setText(QStringLiteral("Не удалось открыть файл"));
        return;
    }
    m_fwData = f.readAll();
    f.close();
    m_file->setText(path);

    const quint32 crc = crc32_ieee(m_fwData);
    m_binHeader = boot::parseFwHeader(m_fwData);

    QString info = QStringLiteral("Размер: %1 байт    CRC32: 0x%2")
                       .arg(m_fwData.size())
                       .arg(crc, 8, 16, QChar('0'));
    if (m_binHeader.valid) {
        info += QStringLiteral("\nОбраз: Product ID 0x%1   hw %2.%3   fw %4.%5")
                    .arg(m_binHeader.productId, 8, 16, QChar('0'))
                    .arg((m_binHeader.hwRev >> 8) & 0xFF, 2, 10, QChar('0'))
                    .arg(m_binHeader.hwRev & 0xFF, 2, 10, QChar('0'))
                    .arg((m_binHeader.fwVersion >> 8) & 0xFF, 2, 10, QChar('0'))
                    .arg(m_binHeader.fwVersion & 0xFF, 2, 10, QChar('0'));
    } else {
        info += QStringLiteral("\nОбраз: нет заголовка fw_header_t (magic 'PLCJ' по 0x200)");
    }
    m_fileInfo->setText(info);
}

void FwUpdateTab::onStatusClicked()
{
    setBusy(true);
    emit requestStatus(m_bootIp->text().trimmed(), quint16(m_port->value()));
}

void FwUpdateTab::onEnterBootloaderClicked()
{
    setBusy(true);
    emit requestEnterBootloader(m_appIp->text().trimmed(), m_bootIp->text().trimmed(),
                                quint16(m_port->value()));
}

void FwUpdateTab::onFlashClicked()
{
    if (m_file->text().isEmpty()) {
        onLog(QStringLiteral("Сначала выберите файл прошивки."));
        return;
    }
    // Identity is read from the image's fw_header_t (see FwWorker). Guard the
    // operator against flashing an image built for a different module: compare
    // the image's product_id with the bootloader's, if we have both.
    if (!m_binHeader.valid) {
        const auto r = QMessageBox::warning(this, QStringLiteral("Нет заголовка"),
            QStringLiteral("В образе не найден заголовок fw_header_t (magic 'PLCJ' по 0x200).\n"
                           "Загрузчик, скорее всего, отклонит такой образ.\n\nПрошить всё равно?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (r != QMessageBox::Yes) return;
    } else if (m_blProductKnown && m_binHeader.productId != m_blProductId) {
        const auto r = QMessageBox::warning(this, QStringLiteral("Несовпадение Product ID"),
            QStringLiteral("Product ID образа (0x%1) не совпадает с Product ID загрузчика (0x%2).\n"
                           "Этот образ предназначен для другого типа модуля.\n\nПрошить всё равно?")
                .arg(m_binHeader.productId, 8, 16, QChar('0'))
                .arg(m_blProductId, 8, 16, QChar('0')),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (r != QMessageBox::Yes) return;
    } else if (!m_blProductKnown) {
        onLog(QStringLiteral("Статус загрузчика неизвестен — сначала нажмите «Статус» "
                             "для проверки Product ID. Продолжаю без сравнения."));
    }

    FwUpdateParams p;
    p.bootIp = m_bootIp->text().trimmed();
    p.appIp = m_appIp->text().trimmed();
    p.port = quint16(m_port->value());
    p.filePath = m_file->text();
    // fwVersion, productId, hwRev are taken from the image's own fw_header_t
    // inside FwWorker::runUpdate — never entered by the user.

    setBusy(true);
    m_progress->setValue(0);
    emit requestUpdate(p);
}

void FwUpdateTab::onAbortClicked()
{
    setBusy(true);
    emit requestAbort(m_bootIp->text().trimmed(), quint16(m_port->value()));
}

void FwUpdateTab::onRebootClicked()
{
    setBusy(true);
    emit requestReboot(m_bootIp->text().trimmed(), quint16(m_port->value()));
}

void FwUpdateTab::onLog(const QString &text)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_log->appendPlainText(QStringLiteral("[%1] %2").arg(ts, text));
}

void FwUpdateTab::onProgress(int done, int total)
{
    if (total <= 0) {
        m_progress->setValue(0);
        return;
    }
    m_progress->setValue(int((done * 100LL) / total));
}

void FwUpdateTab::onStatusUpdated(const boot::Status &s)
{
    m_lBootState->setText(QStringLiteral("%1 (%2)").arg(boot::bootStateName(s.bootState)).arg(s.bootState));
    m_lBootVer->setText(QStringLiteral("%1.%2")
        .arg(s.version >> 8,    2, 10, QChar('0'))
        .arg(s.version & 0xFF,  2, 10, QChar('0')));
    m_lLastError->setText(QStringLiteral("%1 (%2)").arg(boot::errorName(s.lastError)).arg(s.lastError));
    m_lAppValid->setText(QString::number(s.appValid));
    m_lAppVer->setText(s.appValid
        ? QStringLiteral("%1.%2")
              .arg(s.appVersion >> 8,   2, 10, QChar('0'))
              .arg(s.appVersion & 0xFF, 2, 10, QChar('0'))
        : QStringLiteral("—"));
    m_lProductId->setText(QStringLiteral("0x%1").arg(s.productId, 8, 16, QChar('0')));
    m_lAppProductId->setText(s.appValid && s.appProductId
        ? QStringLiteral("0x%1").arg(s.appProductId, 8, 16, QChar('0'))
        : QStringLiteral("—"));
    m_blProductId = s.productId;
    m_blProductKnown = true;
    m_lHwRev->setText(QStringLiteral("%1.%2.%3")
        .arg((s.hwRev >> 16) & 0xFF, 2, 10, QChar('0'))
        .arg((s.hwRev >>  8) & 0xFF, 2, 10, QChar('0'))
        .arg( s.hwRev        & 0xFF, 2, 10, QChar('0')));
    m_lBlocks->setText(QStringLiteral("%1 / %2").arg(s.recvBlocks).arg(s.blockCount));
    m_lImageSize->setText(QStringLiteral("%1 байт").arg(s.imageSize));
    m_lImageCrc->setText(QStringLiteral("0x%1").arg(s.imageCrc, 8, 16, QChar('0')).toUpper()
                             .replace(QStringLiteral("0X"), QStringLiteral("0x")));
    m_lCmdStatus->setText(QStringLiteral("%1 (%2)").arg(boot::cmdStatusName(s.cmdStatus)).arg(s.cmdStatus));
    m_lStaging->setText(QString::number(s.stagingValid));
}

void FwUpdateTab::onFinished(bool ok, const QString &summary)
{
    onLog((ok ? QStringLiteral("✓ ") : QStringLiteral("✗ ")) + summary);
    setBusy(false);
}

void FwUpdateTab::setBusy(bool busy)
{
    for (QPushButton *b : {m_statusBtn, m_bootBtn, m_flashBtn, m_abortBtn, m_rebootBtn, m_selectBtn})
        b->setEnabled(!busy);
}
