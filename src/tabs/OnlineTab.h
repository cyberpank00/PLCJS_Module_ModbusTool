#ifndef ONLINETAB_H
#define ONLINETAB_H

#include <QVector>
#include <QWidget>
#include <memory>

#include "maps/ModuleMaps.h"

class ModbusTcpClient;

QT_BEGIN_NAMESPACE
class QLineEdit;
class QSpinBox;
class QComboBox;
class QTableWidget;
class QLabel;
class QCheckBox;
class QTimer;
QT_END_NAMESPACE

// "Онлайн" tab: register map viewer/editor.
//
// A "Карта модуля" combo selects either the classic free mode (16 rows with
// editable addresses, global FC03/FC04 selector) or a named module map such as
// 12DI, where each row is a labelled register with its own type, an optional
// human-readable decoding and per-register write enablement.
class OnlineTab : public QWidget
{
    Q_OBJECT
public:
    explicit OnlineTab(QWidget *parent = nullptr);
    ~OnlineTab() override;   // out of line: m_pollConn holds an incomplete type here

    static constexpr int kFreeRowCount = 16;

private slots:
    void onRead();
    void onWrite();
    void onMapChanged(int index);

private:
    // Per-row metadata backing the table, filled by buildFreeRows/buildMapRows.
    struct Row {
        bool               addrEditable = false; // free mode: read addr from spin
        quint16            addr = 0;
        maps::RegEntry::Type type = maps::RegEntry::Holding;
        maps::RegEntry::Fmt  fmt = maps::RegEntry::U16;
        bool               writable = false;
        QString (*decode)(quint16) = nullptr;
    };

    // Registers (address + words) expected to read back unchanged after SAVE.
    struct RegExpect {
        quint16          addr = 0;
        QVector<quint16> words;
    };

    void rebuildTable();
    void buildFreeRows();
    void verifySave(const QVector<RegExpect> &expect);
    void buildMapRows(const QVector<maps::RegEntry> &entries);
    QSpinBox *addrSpin(int row) const;
    QLineEdit *writeEdit(int row) const;
    void writeMagic(quint16 addr, quint16 value, const QString &label, bool dangerous);
    void setStatus(const QString &text, bool error = false);
    void setNotice(const QString &text, bool error = false);   // 5 s write/save result

    QComboBox    *m_map;    // "Карта модуля" preset selector
    QLineEdit    *m_ip;
    QSpinBox     *m_port;
    QSpinBox     *m_unitId;
    QComboBox    *m_func;   // free mode only: 0 = Input (FC04), 1 = Holding (FC03)
    QComboBox    *m_base;   // 0 = dec, 1 = hex display
    QTableWidget *m_table;
    QLabel       *m_status;
    QLabel       *m_notice;       // transient write/save result, right of the poll controls
    QTimer       *m_noticeTimer;

    // Continuous polling controls (bottom row).
    QCheckBox    *m_continuous;  // enable periodic re-read
    QSpinBox     *m_period;      // poll period, ms (default 500)
    QSpinBox     *m_timeout;     // per-read Modbus timeout, ms (default 200)
    QTimer       *m_pollTimer;

    // One TCP connection kept open for the whole continuous-read session, so
    // the module sees a single polling client (its STAT_LED "polling" pattern
    // and client-slot accounting depend on that) instead of a connect/close
    // storm every period. Reconnected transparently after a transport error;
    // dropped when the checkbox is cleared. Single reads use a throwaway
    // connection as before.
    std::unique_ptr<ModbusTcpClient> m_pollConn;

    QVector<Row>  m_rows;

    // Column indices (kept in sync with rebuildTable()).
    enum Col { ColName = 0, ColType, ColAddr, ColDec, ColHex, ColDecoded, ColWrite, ColCount };
};

#endif // ONLINETAB_H
