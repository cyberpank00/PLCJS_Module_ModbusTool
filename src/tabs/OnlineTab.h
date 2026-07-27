#ifndef ONLINETAB_H
#define ONLINETAB_H

#include <QVector>
#include <QWidget>

#include "maps/ModuleMaps.h"

QT_BEGIN_NAMESPACE
class QLineEdit;
class QSpinBox;
class QComboBox;
class QTableWidget;
class QLabel;
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

    void rebuildTable();
    void buildFreeRows();
    void buildMapRows(const QVector<maps::RegEntry> &entries);
    QSpinBox *addrSpin(int row) const;
    void setStatus(const QString &text, bool error = false);

    QComboBox    *m_map;    // "Карта модуля" preset selector
    QLineEdit    *m_ip;
    QSpinBox     *m_port;
    QSpinBox     *m_unitId;
    QComboBox    *m_func;   // free mode only: 0 = Input (FC04), 1 = Holding (FC03)
    QComboBox    *m_base;   // 0 = dec, 1 = hex display
    QTableWidget *m_table;
    QLabel       *m_status;

    QVector<Row>  m_rows;

    // Column indices (kept in sync with rebuildTable()).
    enum Col { ColName = 0, ColType, ColAddr, ColDec, ColHex, ColDecoded, ColWrite, ColCount };
};

#endif // ONLINETAB_H
