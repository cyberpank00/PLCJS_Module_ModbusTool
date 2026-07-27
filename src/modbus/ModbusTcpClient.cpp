#include "ModbusTcpClient.h"

#include <QElapsedTimer>
#include <QTcpSocket>

ModbusTcpClient::ModbusTcpClient(int timeoutMs)
    : m_sock(new QTcpSocket)
    , m_unitId(1)
    , m_txId(0)
    , m_timeoutMs(timeoutMs)
{
}

ModbusTcpClient::~ModbusTcpClient()
{
    close();
    delete m_sock;
}

bool ModbusTcpClient::connectToServer(const QString &host, quint16 port, quint8 unitId)
{
    m_unitId = unitId;
    close();
    m_sock->connectToHost(host, port);
    if (!m_sock->waitForConnected(m_timeoutMs)) {
        m_lastError = QStringLiteral("Не удалось подключиться к %1:%2 — %3")
                          .arg(host).arg(port).arg(m_sock->errorString());
        return false;
    }
    m_sock->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    return true;
}

void ModbusTcpClient::close()
{
    if (m_sock->state() != QAbstractSocket::UnconnectedState) {
        m_sock->abort();
    }
}

bool ModbusTcpClient::isConnected() const
{
    return m_sock->state() == QAbstractSocket::ConnectedState;
}

bool ModbusTcpClient::request(const QByteArray &pdu, QByteArray &respPdu)
{
    if (!isConnected()) {
        m_lastError = QStringLiteral("Нет соединения");
        return false;
    }

    m_txId = static_cast<quint16>((m_txId + 1) & 0xFFFF);

    // Assemble MBAP header + PDU.
    QByteArray frame;
    frame.resize(7);
    quint8 *h = reinterpret_cast<quint8 *>(frame.data());
    const quint16 length = static_cast<quint16>(pdu.size() + 1); // unit id + PDU
    h[0] = quint8(m_txId >> 8);
    h[1] = quint8(m_txId & 0xFF);
    h[2] = 0; // protocol id
    h[3] = 0;
    h[4] = quint8(length >> 8);
    h[5] = quint8(length & 0xFF);
    h[6] = m_unitId;
    frame.append(pdu);

    // Drain any stale bytes before issuing a fresh transaction.
    m_sock->readAll();

    if (m_sock->write(frame) != frame.size() || !m_sock->waitForBytesWritten(m_timeoutMs)) {
        m_lastError = QStringLiteral("Ошибка отправки: %1").arg(m_sock->errorString());
        return false;
    }

    // Read until a full MBAP frame for our transaction id is available.
    QByteArray buf;
    QElapsedTimer timer;
    timer.start();
    while (true) {
        if (buf.size() >= 7) {
            const quint8 *b = reinterpret_cast<const quint8 *>(buf.constData());
            const quint16 len = quint16((quint16(b[4]) << 8) | b[5]);
            const int total = 6 + len;
            if (buf.size() >= total) {
                const quint16 rxTx = quint16((quint16(b[0]) << 8) | b[1]);
                if (rxTx != m_txId) {
                    // Discard mismatched frame and keep reading.
                    buf.remove(0, total);
                    continue;
                }
                respPdu = buf.mid(7, total - 7);
                return true;
            }
        }

        const int remaining = m_timeoutMs - int(timer.elapsed());
        if (remaining <= 0) {
            m_lastError = QStringLiteral("Таймаут ответа (txId=%1)").arg(m_txId);
            return false;
        }
        if (!m_sock->waitForReadyRead(remaining)) {
            m_lastError = QStringLiteral("Таймаут чтения: %1").arg(m_sock->errorString());
            return false;
        }
        buf.append(m_sock->readAll());
    }
}

bool ModbusTcpClient::readRegisters(quint8 func, quint16 addr, quint16 qty, QVector<quint16> &out)
{
    QByteArray pdu;
    pdu.resize(5);
    quint8 *p = reinterpret_cast<quint8 *>(pdu.data());
    p[0] = func;
    p[1] = quint8(addr >> 8);
    p[2] = quint8(addr & 0xFF);
    p[3] = quint8(qty >> 8);
    p[4] = quint8(qty & 0xFF);

    QByteArray resp;
    if (!request(pdu, resp))
        return false;

    const quint8 *r = reinterpret_cast<const quint8 *>(resp.constData());
    if (resp.size() >= 2 && r[0] == (func | 0x80)) {
        m_lastError = QStringLiteral("Modbus исключение %1 (функция %2)").arg(r[1]).arg(func);
        return false;
    }
    if (resp.size() < 2 || r[0] != func) {
        m_lastError = QStringLiteral("Некорректный ответ на функцию %1").arg(func);
        return false;
    }
    const int byteCount = r[1];
    if (resp.size() < 2 + byteCount) {
        m_lastError = QStringLiteral("Неполный ответ (ожидалось %1 байт данных)").arg(byteCount);
        return false;
    }
    out.clear();
    for (int i = 0; i + 1 < byteCount; i += 2)
        out.append(quint16((quint16(r[2 + i]) << 8) | r[2 + i + 1]));
    return true;
}

bool ModbusTcpClient::readHoldingRegisters(quint16 addr, quint16 qty, QVector<quint16> &out)
{
    return readRegisters(0x03, addr, qty, out);
}

bool ModbusTcpClient::readInputRegisters(quint16 addr, quint16 qty, QVector<quint16> &out)
{
    return readRegisters(0x04, addr, qty, out);
}

bool ModbusTcpClient::writeSingleRegister(quint16 addr, quint16 value)
{
    QByteArray pdu;
    pdu.resize(5);
    quint8 *p = reinterpret_cast<quint8 *>(pdu.data());
    p[0] = 0x06;
    p[1] = quint8(addr >> 8);
    p[2] = quint8(addr & 0xFF);
    p[3] = quint8(value >> 8);
    p[4] = quint8(value & 0xFF);

    QByteArray resp;
    if (!request(pdu, resp))
        return false;
    const quint8 *r = reinterpret_cast<const quint8 *>(resp.constData());
    if (resp.size() >= 2 && r[0] == (0x06 | 0x80)) {
        m_lastError = QStringLiteral("Modbus исключение %1 (FC06)").arg(r[1]);
        return false;
    }
    return true;
}

bool ModbusTcpClient::writeMultipleRegisters(quint16 addr, const QVector<quint16> &values)
{
    const int qty = values.size();
    const int byteCount = qty * 2;
    QByteArray pdu;
    pdu.resize(6 + byteCount);
    quint8 *p = reinterpret_cast<quint8 *>(pdu.data());
    p[0] = 0x10;
    p[1] = quint8(addr >> 8);
    p[2] = quint8(addr & 0xFF);
    p[3] = quint8(quint16(qty) >> 8);
    p[4] = quint8(quint16(qty) & 0xFF);
    p[5] = quint8(byteCount);
    for (int i = 0; i < qty; ++i) {
        p[6 + i * 2] = quint8(values[i] >> 8);
        p[6 + i * 2 + 1] = quint8(values[i] & 0xFF);
    }

    QByteArray resp;
    if (!request(pdu, resp))
        return false;
    const quint8 *r = reinterpret_cast<const quint8 *>(resp.constData());
    if (resp.size() >= 2 && r[0] == (0x10 | 0x80)) {
        m_lastError = QStringLiteral("Modbus исключение %1 (FC16)").arg(r[1]);
        return false;
    }
    return true;
}
