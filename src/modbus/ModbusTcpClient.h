#ifndef MODBUSTCPCLIENT_H
#define MODBUSTCPCLIENT_H

#include <QByteArray>
#include <QString>
#include <QVector>
#include <cstdint>

QT_BEGIN_NAMESPACE
class QTcpSocket;
QT_END_NAMESPACE

// Minimal synchronous Modbus TCP client built directly on QTcpSocket.
// MBAP + PDU are assembled by hand, mirroring the client implemented in
// tools/fw_update.mjs. Blocking calls use a bounded timeout so the caller
// (a worker thread or a quick foreground read) stays predictable.
//
// All methods return false on error and fill lastError().
class ModbusTcpClient
{
public:
    explicit ModbusTcpClient(int timeoutMs = 5000);
    ~ModbusTcpClient();

    void setTimeout(int ms) { m_timeoutMs = ms; }

    bool connectToServer(const QString &host, quint16 port, quint8 unitId = 1);
    void close();
    bool isConnected() const;

    // FC03 read holding registers.
    bool readHoldingRegisters(quint16 addr, quint16 qty, QVector<quint16> &out);
    // FC04 read input registers.
    bool readInputRegisters(quint16 addr, quint16 qty, QVector<quint16> &out);
    // FC06 write single register.
    bool writeSingleRegister(quint16 addr, quint16 value);
    // FC16 write multiple registers.
    bool writeMultipleRegisters(quint16 addr, const QVector<quint16> &values);

    QString lastError() const { return m_lastError; }

private:
    bool readRegisters(quint8 func, quint16 addr, quint16 qty, QVector<quint16> &out);
    // Sends a PDU and returns the response PDU (function code onward).
    bool request(const QByteArray &pdu, QByteArray &respPdu);

    QTcpSocket *m_sock;
    quint8      m_unitId;
    quint16     m_txId;
    int         m_timeoutMs;
    QString     m_lastError;
};

#endif // MODBUSTCPCLIENT_H
