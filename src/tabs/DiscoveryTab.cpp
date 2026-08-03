#include "DiscoveryTab.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUdpSocket>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// PLCJS Discovery Protocol (PDP) — must match Application/discovery/discovery.c
// ---------------------------------------------------------------------------
namespace {
constexpr quint16 kPort       = 20556;
constexpr quint8  kVersion    = 1;
constexpr quint8  kRespFlag   = 0x80;
constexpr int     kHdrLen     = 16;

constexpr quint8 kOpIdentify  = 0x01;
constexpr quint8 kOpSetNet    = 0x02;
constexpr quint8 kOpSetName   = 0x03;
constexpr quint8 kOpFlashLed  = 0x04;
constexpr quint8 kOpReboot    = 0x05;
constexpr quint8 kOpFactory   = 0x06;

QString macToStr(const QByteArray &m)
{
    QStringList parts;
    for (unsigned char b : m)
        parts << QString("%1").arg(b, 2, 16, QChar('0')).toUpper();
    return parts.join(':');
}

QString modeName(quint8 m)
{
    switch (m) {
    case 0: return QStringLiteral("статический");
    case 1: return QStringLiteral("DHCP");
    case 2: return QStringLiteral("link-local");
    default: return QString::number(m);
    }
}

QString productName(quint32 id)
{
    switch (id) {
    case 0x504C1201u: return QStringLiteral("12DI");
    case 0x504C1202u: return QStringLiteral("12DO");
    case 0x504C0403u: return QStringLiteral("4RTD");
    default: return QString("0x%1").arg(id, 8, 16, QChar('0')).toUpper();
    }
}

// "a.b.c.d" -> 4 bytes; returns false if malformed.
bool parseIpv4(const QString &s, quint8 out[4])
{
    const QStringList parts = s.split('.');
    if (parts.size() != 4)
        return false;
    for (int i = 0; i < 4; ++i) {
        bool ok = false;
        const int v = parts[i].trimmed().toInt(&ok);
        if (!ok || v < 0 || v > 255)
            return false;
        out[i] = quint8(v);
    }
    return true;
}
} // namespace

DiscoveryTab::DiscoveryTab(QWidget *parent)
    : QWidget(parent)
{
    // ---- NIC + scan ------------------------------------------------------
    m_nic = new QComboBox;
    m_nic->setMinimumWidth(260);
    auto *refreshBtn = new QPushButton(QStringLiteral("⟳"));
    refreshBtn->setToolTip(QStringLiteral("Обновить список сетевых адаптеров"));
    refreshBtn->setFixedWidth(32);
    auto *scanBtn = new QPushButton(QStringLiteral("Сканировать"));

    auto *nicRow = new QHBoxLayout;
    nicRow->addWidget(new QLabel(QStringLiteral("Сетевой адаптер:")));
    nicRow->addWidget(m_nic, 1);
    nicRow->addWidget(refreshBtn);
    nicRow->addWidget(scanBtn);

    // ---- Device table ----------------------------------------------------
    m_table = new QTableWidget(0, ColCount);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("MAC"), QStringLiteral("Тип"), QStringLiteral("FW"),
         QStringLiteral("Режим"), QStringLiteral("IP"), QStringLiteral("Маска"),
         QStringLiteral("Имя"), QStringLiteral("Загрузчик")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    auto *hh = m_table->horizontalHeader();
    hh->setSectionResizeMode(ColMac, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(ColName, QHeaderView::Stretch);

    // ---- Assignment panel ------------------------------------------------
    m_mode = new QComboBox;
    m_mode->addItem(QStringLiteral("Статический"));   // 0
    m_mode->addItem(QStringLiteral("DHCP"));          // 1
    m_mode->addItem(QStringLiteral("Link-local"));    // 2
    m_ip   = new QLineEdit;
    m_mask = new QLineEdit(QStringLiteral("255.255.255.0"));
    m_gw   = new QLineEdit(QStringLiteral("192.168.1.1"));
    m_applyNet = new QPushButton(QStringLiteral("Применить сеть"));

    auto *netBox = new QGroupBox(QStringLiteral("Назначить сеть (выбранному)"));
    auto *netForm = new QFormLayout(netBox);
    netForm->addRow(QStringLiteral("Режим:"), m_mode);
    netForm->addRow(QStringLiteral("IP:"), m_ip);
    netForm->addRow(QStringLiteral("Маска:"), m_mask);
    netForm->addRow(QStringLiteral("Шлюз:"), m_gw);
    netForm->addRow(m_applyNet);

    m_name = new QLineEdit;
    m_name->setMaxLength(15);
    m_setName = new QPushButton(QStringLiteral("Задать имя"));
    m_flash   = new QPushButton(QStringLiteral("Мигнуть LED (5с)"));
    m_reboot  = new QPushButton(QStringLiteral("Перезагрузить"));
    m_factory = new QPushButton(QStringLiteral("Сброс к заводским"));

    auto *actBox = new QGroupBox(QStringLiteral("Действия (выбранному)"));
    auto *actForm = new QFormLayout(actBox);
    actForm->addRow(QStringLiteral("Имя:"), m_name);
    actForm->addRow(m_setName);
    actForm->addRow(m_flash);
    actForm->addRow(m_reboot);
    actForm->addRow(m_factory);

    auto *sidePanel = new QVBoxLayout;
    sidePanel->addWidget(netBox);
    sidePanel->addWidget(actBox);
    sidePanel->addStretch();

    auto *mid = new QHBoxLayout;
    mid->addWidget(m_table, 1);
    mid->addLayout(sidePanel, 0);

    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setText(QStringLiteral(
        "Готово. Ответы приходят UDP-широковещанием на порт 20556 — "
        "разрешите входящий UDP:20556 в брандмауэре, иначе устройства не появятся."));

    auto *root = new QVBoxLayout(this);
    root->addLayout(nicRow);
    root->addLayout(mid, 1);
    root->addWidget(m_status);

    // ---- Socket ----------------------------------------------------------
    m_rx = new QUdpSocket(this);
    if (!m_rx->bind(QHostAddress::AnyIPv4, kPort,
                    QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        setStatus(QStringLiteral("Не удалось открыть UDP:20556 — ") +
                      m_rx->errorString(), true);
    }
    connect(m_rx, &QUdpSocket::readyRead, this, &DiscoveryTab::onRxReady);

    // ---- Signals ---------------------------------------------------------
    connect(refreshBtn, &QPushButton::clicked, this, &DiscoveryTab::refreshNics);
    connect(scanBtn, &QPushButton::clicked, this, &DiscoveryTab::onScan);
    connect(m_table, &QTableWidget::itemSelectionChanged, this,
            &DiscoveryTab::onSelectionChanged);
    connect(m_mode, &QComboBox::currentIndexChanged, this, &DiscoveryTab::onModeChanged);
    connect(m_applyNet, &QPushButton::clicked, this, &DiscoveryTab::onApplyNet);
    connect(m_flash, &QPushButton::clicked, this, &DiscoveryTab::onFlashLed);
    connect(m_setName, &QPushButton::clicked, this, &DiscoveryTab::onSetName);
    connect(m_reboot, &QPushButton::clicked, this, &DiscoveryTab::onReboot);
    connect(m_factory, &QPushButton::clicked, this, &DiscoveryTab::onFactory);

    refreshNics();
    onSelectionChanged(); // disable action widgets until a row is selected
    onModeChanged();
}

// ---------------------------------------------------------------------------
void DiscoveryTab::refreshNics()
{
    m_nic->clear();
    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &ifc : ifaces) {
        if (!(ifc.flags() & QNetworkInterface::IsUp) ||
            !(ifc.flags() & QNetworkInterface::IsRunning) ||
            (ifc.flags() & QNetworkInterface::IsLoopBack))
            continue;
        for (const QNetworkAddressEntry &e : ifc.addressEntries()) {
            const QHostAddress a = e.ip();
            if (a.protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            const QString label = QString("%1 — %2").arg(ifc.humanReadableName(), a.toString());
            m_nic->addItem(label, a.toString());
        }
    }
    if (m_nic->count() == 0)
        setStatus(QStringLiteral("Нет активных IPv4-адаптеров"), true);
}

void DiscoveryTab::onScan()
{
    m_devices.clear();
    m_table->setRowCount(0);
    onSelectionChanged();
    sendCommand(kOpIdentify, QByteArray(6, '\0'), QByteArray());
    setStatus(QStringLiteral("Отправлен IDENTIFY (broadcast). Ожидание ответов…"));
}

// Build a PDP frame and broadcast it out of the selected NIC.
void DiscoveryTab::sendCommand(quint8 opcode, const QByteArray &targetMac,
                               const QByteArray &payload)
{
    if (m_nic->count() == 0) {
        setStatus(QStringLiteral("Не выбран сетевой адаптер"), true);
        return;
    }
    const QHostAddress nic(m_nic->currentData().toString());
    const quint16 txid = m_txid++;

    QByteArray f(kHdrLen, '\0');
    f[0] = 'P'; f[1] = 'L'; f[2] = 'C'; f[3] = 'D';
    f[4] = char(kVersion);
    f[5] = char(opcode);
    f[6] = char((txid >> 8) & 0xFF);
    f[7] = char(txid & 0xFF);
    QByteArray mac = targetMac.leftJustified(6, '\0', true);
    for (int i = 0; i < 6; ++i)
        f[8 + i] = mac[i];
    const quint16 plen = quint16(payload.size());
    f[14] = char((plen >> 8) & 0xFF);
    f[15] = char(plen & 0xFF);
    f.append(payload);

    // Bind a transient socket to the chosen NIC so the broadcast egresses that
    // interface (a host can have several IPv4 / link-local interfaces).
    QUdpSocket tx;
    if (!tx.bind(nic, 0)) {
        setStatus(QStringLiteral("Не удалось привязать сокет к ") + nic.toString() +
                      QStringLiteral(": ") + tx.errorString(), true);
        return;
    }
    const qint64 n = tx.writeDatagram(f, QHostAddress::Broadcast, kPort);
    tx.flush();
    tx.waitForBytesWritten(200);
    if (n != f.size())
        setStatus(QStringLiteral("Ошибка отправки: ") + tx.errorString(), true);
}

void DiscoveryTab::onRxReady()
{
    while (m_rx->hasPendingDatagrams()) {
        QByteArray buf;
        buf.resize(int(m_rx->pendingDatagramSize()));
        m_rx->readDatagram(buf.data(), buf.size());

        if (buf.size() < kHdrLen)
            continue;
        if (!(buf[0] == 'P' && buf[1] == 'L' && buf[2] == 'C' && buf[3] == 'D'))
            continue;
        if (quint8(buf[4]) != kVersion)
            continue;
        const quint8 opcode = quint8(buf[5]);
        if (!(opcode & kRespFlag))
            continue; // ignore requests (incl. our own broadcast loopback)

        const auto u8 = [&](int i) { return quint8(buf[i]); };
        const QByteArray mac = buf.mid(8, 6);
        const int plStart = kHdrLen;

        if (opcode == (kOpIdentify | kRespFlag)) {
            if (buf.size() < plStart + 38)
                continue;
            const uchar *p = reinterpret_cast<const uchar *>(buf.constData()) + plStart;
            Device d;
            d.mac = mac;
            d.productId = (quint32(p[0]) << 24) | (quint32(p[1]) << 16) |
                          (quint32(p[2]) << 8) | quint32(p[3]);
            d.hw = quint16((p[4] << 8) | p[5]);
            d.fw = quint16((p[6] << 8) | p[7]);
            d.netMode = p[8];
            d.inBoot = p[9];
            d.ip   = QString("%1.%2.%3.%4").arg(p[10]).arg(p[11]).arg(p[12]).arg(p[13]);
            d.mask = QString("%1.%2.%3.%4").arg(p[14]).arg(p[15]).arg(p[16]).arg(p[17]);
            d.gw   = QString("%1.%2.%3.%4").arg(p[18]).arg(p[19]).arg(p[20]).arg(p[21]);
            d.name = QString::fromLatin1(buf.mid(plStart + 22, 16)).trimmed();
            d.name.remove(QChar('\0'));
            addOrUpdateDevice(d);
        } else {
            // Command ack (SET_NET/SET_NAME/FLASH_LED/REBOOT/FACTORY).
            const quint8 status = (buf.size() > plStart) ? u8(plStart) : 0xFF;
            setStatus(QString("%1: ответ 0x%2 статус %3")
                          .arg(macToStr(mac))
                          .arg(opcode, 2, 16, QChar('0'))
                          .arg(status == 0 ? QStringLiteral("OK")
                                           : QString("ошибка(%1)").arg(status)),
                      status != 0);
        }
    }
}

void DiscoveryTab::addOrUpdateDevice(const Device &d)
{
    int row = -1;
    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices[i].mac == d.mac) { row = i; break; }
    }
    if (row < 0) {
        row = m_devices.size();
        m_devices.append(d);
        m_table->insertRow(row);
        for (int c = 0; c < ColCount; ++c)
            m_table->setItem(row, c, new QTableWidgetItem);
    } else {
        m_devices[row] = d;
    }

    m_table->item(row, ColMac)->setText(macToStr(d.mac));
    m_table->item(row, ColType)->setText(productName(d.productId));
    m_table->item(row, ColFw)->setText(QString("%1.%2").arg(d.fw >> 8).arg(d.fw & 0xFF));
    m_table->item(row, ColMode)->setText(modeName(d.netMode));
    m_table->item(row, ColIp)->setText(d.ip);
    m_table->item(row, ColMask)->setText(d.mask);
    m_table->item(row, ColName)->setText(d.name);
    m_table->item(row, ColBoot)->setText(d.inBoot ? QStringLiteral("да") : QString());

    setStatus(QString("Найдено устройств: %1").arg(m_devices.size()));
}

int DiscoveryTab::selectedRow() const
{
    const auto sel = m_table->selectionModel()->selectedRows();
    return sel.isEmpty() ? -1 : sel.first().row();
}

QByteArray DiscoveryTab::selectedMac() const
{
    const int r = selectedRow();
    return (r >= 0 && r < m_devices.size()) ? m_devices[r].mac : QByteArray();
}

void DiscoveryTab::onSelectionChanged()
{
    const int r = selectedRow();
    const bool has = (r >= 0 && r < m_devices.size());
    const QList<QWidget *> ws = {m_applyNet, m_flash, m_setName,
                                 m_reboot, m_factory, m_mode, m_name};
    for (QWidget *w : ws)
        w->setEnabled(has);
    if (!has) { onModeChanged(); return; }

    const Device &d = m_devices[r];
    m_mode->setCurrentIndex(qMin<int>(d.netMode, 2));
    m_ip->setText(d.ip);
    if (d.mask != QStringLiteral("0.0.0.0"))
        m_mask->setText(d.mask);
    if (d.gw != QStringLiteral("0.0.0.0"))
        m_gw->setText(d.gw);
    m_name->setText(d.name);
    onModeChanged();
}

void DiscoveryTab::onModeChanged()
{
    // IP/mask/gw only matter for the static mode.
    const bool isStatic = (m_mode->currentIndex() == 0) && m_mode->isEnabled();
    m_ip->setEnabled(isStatic);
    m_mask->setEnabled(isStatic);
    m_gw->setEnabled(isStatic);
}

void DiscoveryTab::onApplyNet()
{
    const QByteArray mac = selectedMac();
    if (mac.isEmpty()) return;
    const int mode = m_mode->currentIndex();

    QByteArray pl;
    pl.append(char(mode));
    if (mode == 0) { // static: append ip/mask/gw
        quint8 ip[4], mk[4], gw[4];
        if (!parseIpv4(m_ip->text(), ip) || !parseIpv4(m_mask->text(), mk) ||
            !parseIpv4(m_gw->text(), gw)) {
            setStatus(QStringLiteral("Неверный формат IP/маски/шлюза"), true);
            return;
        }
        for (int i = 0; i < 4; ++i) pl.append(char(ip[i]));
        for (int i = 0; i < 4; ++i) pl.append(char(mk[i]));
        for (int i = 0; i < 4; ++i) pl.append(char(gw[i]));
    }
    sendCommand(kOpSetNet, mac, pl);
    setStatus(QString("SET_NET → %1 (%2)").arg(macToStr(mac), modeName(quint8(mode))));
}

void DiscoveryTab::onFlashLed()
{
    const QByteArray mac = selectedMac();
    if (mac.isEmpty()) return;
    QByteArray pl;
    pl.append(char(5)); // 5 seconds
    sendCommand(kOpFlashLed, mac, pl);
}

void DiscoveryTab::onSetName()
{
    const QByteArray mac = selectedMac();
    if (mac.isEmpty()) return;
    sendCommand(kOpSetName, mac, m_name->text().toLatin1().left(15));
    setStatus(QString("SET_NAME → %1").arg(macToStr(mac)));
}

void DiscoveryTab::onReboot()
{
    const QByteArray mac = selectedMac();
    if (mac.isEmpty()) return;
    if (QMessageBox::question(this, QStringLiteral("Перезагрузка"),
                              QStringLiteral("Перезагрузить %1?").arg(macToStr(mac)))
        != QMessageBox::Yes)
        return;
    sendCommand(kOpReboot, mac, QByteArray());
}

void DiscoveryTab::onFactory()
{
    const QByteArray mac = selectedMac();
    if (mac.isEmpty()) return;
    if (QMessageBox::warning(this, QStringLiteral("Сброс к заводским"),
                             QStringLiteral("Сбросить %1 к заводским настройкам? "
                                            "Сетевая конфигурация будет потеряна.")
                                 .arg(macToStr(mac)),
                             QMessageBox::Yes | QMessageBox::No)
        != QMessageBox::Yes)
        return;
    sendCommand(kOpFactory, mac, QByteArray());
}

void DiscoveryTab::setStatus(const QString &text, bool error)
{
    m_status->setStyleSheet(error ? QStringLiteral("color:#b00020;")
                                  : QString());
    m_status->setText(text);
}
