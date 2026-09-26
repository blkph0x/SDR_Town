#pragma once
#include <QWidget>
#include <QMap>
#include "InmarsatEngine.h"

class QTableWidget;
class QPlainTextEdit;
class QLabel;
class QCheckBox;
class QTimer;

// Passive GUI snapshots only: never retains a decoder/worker callback.
class InmarsatMonitorWidget : public QWidget {
public:
    enum class View { Decoders, Aircraft };
    explicit InmarsatMonitorWidget(View view, QWidget* parent = nullptr, bool popout = true);
    void updateSnapshot(const InmarsatEngineSnapshot& snapshot,
                        const std::vector<InmarsatAircraft>& aircraft, double now);
    QString tableText() const;
protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
private:
    void refresh();
    View view_;
    QTableWidget* table_;
    QLabel* count_;
    QCheckBox* positions_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
    QTimer* timer_;
    QMap<QString, QString> states_;
};
