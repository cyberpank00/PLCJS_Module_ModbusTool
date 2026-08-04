#include "Pdp.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QMap>
#include <QNetworkInterface>
#include <QThread>
#include <QUdpSocket>

namespace pdp {
namespace {

constexpr quint8 kVer = 1;
constexpr quint8 kResp = 0x80;
constexpr quint8 kOpIdentify = 0x01;
constexpr quint8 kOpSetNet = 0x02;

QByteArray buildHeader(quint8 opcode, quint16 txid, const QByteArray &mac6, quint16 plen)
{
    QByteArray h(16, '\0');
    h[0] = 'P'; h[1] = 'L'; h[2] = 'C'; h[3] = 'D';
    h[4] = char(kVer);
    h[5] = char(opcode);
    h[6] = char((txid >> 8) & 0xFF);
    h[7] = char(txid & 0xFF);
    const QByteArray m = mac6.leftJustified(6, '\0', true);
    for (int i = 0; i < 6; ++i) h[8 + i] = m[i];
    h[14] = char((plen >> 8) & 0xFF);
    h[15] = char(plen & 0xFF);
    return h;
}

Device parseIdentify(const QByteArray &buf)
{
    Device d;
    if (buf.size() < 16 || buf.left(4) != QByteArray("PLCD")) return d;
    const quint8 opcode = quint8(buf[5]);
    if (opcode != (kOpIdentify | kResp)) return d;
    d.mac = buf.mid(8, 6);
    QStringList mp;
    for (unsigned char b : d.mac) mp << QString("%1").arg(b, 2, 16, QChar('0'));
    d.macStr = mp.join(':');
    const QByteArray p = buf.mid(16);
    if (p.size() < 38) { d.mac.clear(); return d; }
    const uchar *u = reinterpret_cast<const uchar *>(p.constData());
    d.productId = (quint32(u[0]) << 24) | (quint32(u[1]) << 16) | (quint32(u[2]) << 8) | u[3];
    d.fw = quint16((u[6] << 8) | u[7]);
    d.netMode = u[8];
    d.inBoot = u[9] != 0;
    d.ip = QString("%1.%2.%3.%4").arg(u[10]).arg(u[11]).arg(u[12]).arg(u[13]);
    d.mask = QString("%1.%2.%3.%4").arg(u[14]).arg(u[15]).arg(u[16]).arg(u[17]);
    d.name = QString::fromLatin1(p.mid(22, 16)).remove(QChar('\0')).trimmed();
    return d;
}

QByteArray octets(const QString &ip)
{
    QByteArray b;
    for (const QString &s : ip.split('.')) b.append(char(s.toInt() & 0xFF));
    return b.leftJustified(4, '\0', true);
}

} // namespace

QList<Device> discover(const QString &nicIp, int timeoutMs)
{
    QList<Device> out;
    QUdpSocket rx;
    if (!rx.bind(QHostAddress::AnyIPv4, kPort,
                 QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        return out;
    }
    QUdpSocket tx;
    if (!nicIp.isEmpty()) tx.bind(QHostAddress(nicIp), 0);
    else tx.bind(QHostAddress::AnyIPv4, 0);

    const QByteArray req = buildHeader(kOpIdentify, 0xF00D, QByteArray(), 0);
    tx.writeDatagram(req, QHostAddress::Broadcast, kPort);

    QMap<QString, Device> found;
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs) {
        if (rx.waitForReadyRead(qMax(1, timeoutMs - int(t.elapsed())))) {
            while (rx.hasPendingDatagrams()) {
                QByteArray buf;
                buf.resize(int(rx.pendingDatagramSize()));
                rx.readDatagram(buf.data(), buf.size());
                const Device d = parseIdentify(buf);
                if (!d.mac.isEmpty()) found.insert(d.macStr, d);
            }
        }
    }
    return found.values();
}

bool findBootloader(const QString &nicIp, const QString &macStr, int timeoutMs, Device &out)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs) {
        QList<Device> bl;
        for (const Device &d : discover(nicIp, 1500)) {
            if (d.inBoot && (macStr.isEmpty() || d.macStr == macStr)) bl.append(d);
        }
        if (!macStr.isEmpty()) {
            for (const Device &d : bl) if (d.macStr == macStr) { out = d; return true; }
        } else if (bl.size() == 1) {
            out = bl.first();
            return true;
        }
    }
    return false;
}

QString macForIp(const QString &nicIp, const QString &ip, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs) {
        for (const Device &d : discover(nicIp, 1500))
            if (d.ip == ip) return d.macStr;
    }
    return QString();
}

void setNetStatic(const QString &nicIp, const QString &macStr,
                  const QString &ip, const QString &mask, const QString &gw)
{
    QByteArray mac6;
    for (const QString &h : macStr.split(':')) mac6.append(char(h.toInt(nullptr, 16)));

    QByteArray payload;
    payload.append(char(0)); // mode = static
    payload.append(octets(ip));
    payload.append(octets(mask));
    payload.append(octets(gw));

    const QByteArray f = buildHeader(kOpSetNet, 0x5E70, mac6, quint16(payload.size())) + payload;

    QUdpSocket tx;
    if (!nicIp.isEmpty()) tx.bind(QHostAddress(nicIp), 0);
    else tx.bind(QHostAddress::AnyIPv4, 0);
    for (int i = 0; i < 2; ++i) {
        tx.writeDatagram(f, QHostAddress::Broadcast, kPort);
        tx.waitForBytesWritten(200);
        QThread::msleep(200);
    }
}

QString nicForPeer(const QString &peerIp)
{
    const QHostAddress peer(peerIp);
    if (peer.isNull()) return QString();
    for (const QNetworkInterface &ifc : QNetworkInterface::allInterfaces()) {
        if (!(ifc.flags() & QNetworkInterface::IsUp) ||
            (ifc.flags() & QNetworkInterface::IsLoopBack))
            continue;
        for (const QNetworkAddressEntry &e : ifc.addressEntries()) {
            if (e.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            if (e.prefixLength() > 0 &&
                peer.isInSubnet(e.ip(), e.prefixLength())) {
                return e.ip().toString();
            }
        }
    }
    return QString();
}

} // namespace pdp
