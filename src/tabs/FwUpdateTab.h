#ifndef FWUPDATETAB_H
#define FWUPDATETAB_H

#include <QWidget>

#include "FwWorker.h"
#include "protocol/BootloaderProtocol.h"

QT_BEGIN_NAMESPACE
class QLineEdit;
class QSpinBox;
class QPushButton;
class QProgressBar;
class QPlainTextEdit;
class QLabel;
class QThread;
QT_END_NAMESPACE

// "Обновление FW" tab: select a .bin, put the module into bootloader mode,
// and flash it over Modbus TCP. All blocking work runs on a worker thread.
class FwUpdateTab : public QWidget
{
    Q_OBJECT
public:
    explicit FwUpdateTab(QWidget *parent = nullptr);
    ~FwUpdateTab() override;

signals:
    void requestStatus(const QString &ip, quint16 port);
    void requestEnterBootloader(const QString &appIp, const QString &bootIp, quint16 port);
    void requestUpdate(const FwUpdateParams &p);
    void requestAbort(const QString &bootIp, quint16 port);
    void requestReboot(const QString &bootIp, quint16 port);

private slots:
    void onSelectFile();
    void onStatusClicked();
    void onEnterBootloaderClicked();
    void onFlashClicked();
    void onAbortClicked();
    void onRebootClicked();

    void onLog(const QString &text);
    void onProgress(int done, int total);
    void onStatusUpdated(const boot::Status &s);
    void onFinished(bool ok, const QString &summary);

private:
    void setBusy(bool busy);
    quint32 parseHex(const QLineEdit *edit, quint32 fallback) const;

    QLineEdit      *m_appIp;
    QLineEdit      *m_bootIp;
    QSpinBox       *m_port;
    QLineEdit      *m_fwVersion;
    QLineEdit      *m_productId;
    QSpinBox       *m_hwRev;

    QLineEdit      *m_file;
    QLabel         *m_fileInfo;
    QByteArray      m_fwData;

    QPushButton    *m_statusBtn;
    QPushButton    *m_bootBtn;
    QPushButton    *m_flashBtn;
    QPushButton    *m_abortBtn;
    QPushButton    *m_rebootBtn;
    QPushButton    *m_selectBtn;

    QProgressBar   *m_progress;
    QPlainTextEdit *m_log;

    // Status panel labels.
    QLabel *m_lBootState;
    QLabel *m_lLastError;
    QLabel *m_lAppValid;
    QLabel *m_lAppVer;
    QLabel *m_lProductId;
    QLabel *m_lHwRev;
    QLabel *m_lBlocks;
    QLabel *m_lImageSize;
    QLabel *m_lImageCrc;
    QLabel *m_lCmdStatus;
    QLabel *m_lStaging;

    QThread  *m_thread;
    FwWorker *m_worker;
};

#endif // FWUPDATETAB_H
