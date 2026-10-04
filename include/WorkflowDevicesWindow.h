#pragma once
#include "DeviceOwnership.h"
#include <QDialog>
#include <functional>

class QTableWidget;
class QLabel;

// DEC-0181: the UI edits reservations, not driver handles. Providers also let
// widget tests exercise the exact interaction without opening physical radios.
class WorkflowDevicesWindow final : public QDialog {
public:
    struct Row {
        QString key, label, status;
        bool editable = true;
    };
    struct Snapshot {
        std::vector<Row> rows;
        DeviceOwnership::Assignments assignments;
    };
    using Read = std::function<Snapshot()>;
    using Save = std::function<QString(const DeviceOwnership::Assignments&)>;
    WorkflowDevicesWindow(Read read, Save save, QWidget* parent = nullptr);
private:
    void refresh();
    Read read_;
    Save save_;
    Snapshot snapshot_;
    QTableWidget* table_;
    QLabel* status_;
};
