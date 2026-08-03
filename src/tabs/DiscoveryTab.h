#ifndef DISCOVERYTAB_H
#define DISCOVERYTAB_H

#include <QByteArray>
#include <QVector>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QLabel;
class QUdpSocket;
QT_END_NAMESPACE

// "Обнаружение" tab: PLCJS Discovery Protocol (PDP) client.
//
// Broadcasts UDP IDENTIFY frames on port 20556 out of a chosen NIC and lists
// every module that answers (by MAC), regardless of its IP/subnet — so a
// factory-fresh link-local device or one on the wrong subnet can still be found
// and reassigned. The selected device can be given a new network config
// (static/DHCP/link-local, applied live), a name, a flashing identify LED, a
// reboot or a factory reset.
//
// NOTE: the module answers via UDP broadcast, so the host firewall must allow
// inbound UDP:20556 or the responses are silently dropped.
class DiscoveryTab : public QWidget
{
    Q_OBJECT
public:
    explicit DiscoveryTab(QWidget *parent = nullptr);

private slots:
    void refreshNics();
    void onScan();
    void onRxReady();
    void onApplyNet();
    void onFlashLed();
    void onSetName();
    void onReboot();
    void onFactory();
    void onSelectionChanged();
    void onModeChanged();

private:
    struct Device {
        QByteArray mac;      // 6 bytes
        quint32    productId = 0;
        quint16    hw = 0;
        quint16    fw = 0;
        quint8     netMode = 0;   // 0=static, 1=dhcp, 2=link-local
        quint8     inBoot = 0;
        QString    ip, mask, gw, name;
    };

    void        sendCommand(quint8 opcode, const QByteArray &targetMac,
                            const QByteArray &payload);
    void        addOrUpdateDevice(const Device &d);
    int         selectedRow() const;
    QByteArray  selectedMac() const;
    void        setStatus(const QString &text, bool error = false);

    QComboBox    *m_nic;
    QTableWidget *m_table;
    QComboBox    *m_mode;
    QLineEdit    *m_ip;
    QLineEdit    *m_mask;
    QLineEdit    *m_gw;
    QLineEdit    *m_name;
    QPushButton  *m_applyNet;
    QPushButton  *m_flash;
    QPushButton  *m_setName;
    QPushButton  *m_reboot;
    QPushButton  *m_factory;
    QLabel       *m_status;

    QUdpSocket   *m_rx = nullptr;   // bound to Any:20556, receives responses
    QVector<Device> m_devices;      // parallel to table rows
    quint16       m_txid = 1;

    enum Col { ColMac = 0, ColType, ColFw, ColMode, ColIp, ColMask, ColName, ColBoot, ColCount };
};

#endif // DISCOVERYTAB_H
