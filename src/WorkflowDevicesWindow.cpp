#include "WorkflowDevicesWindow.h"
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QStyle>

WorkflowDevicesWindow::WorkflowDevicesWindow(Read read, Save save, QWidget* parent)
    : QDialog(parent), read_(std::move(read)), save_(std::move(save)) {
    setWindowTitle("Workflow Devices");
    setObjectName("workflowDevicesWindow");
    resize(800, 360);
    auto* layout = new QVBoxLayout(this);
    table_ = new QTableWidget(this);
    table_->setObjectName("workflowDevicesTable");
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels({"Radio", "Assigned workflow", "Status"});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    layout->addWidget(table_);
    status_ = new QLabel(this); status_->setWordWrap(true); status_->setObjectName("workflowDevicesStatus");
    layout->addWidget(status_);
    auto* buttons = new QHBoxLayout;
    auto* refreshButton = new QPushButton(style()->standardIcon(QStyle::SP_BrowserReload), "Refresh", this);
    refreshButton->setObjectName("workflowDevicesRefresh");
    auto* apply = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton), "Save assignments", this);
    apply->setObjectName("workflowDevicesSave");
    auto* close = new QPushButton("Close", this);
    buttons->addWidget(refreshButton); buttons->addStretch(); buttons->addWidget(apply); buttons->addWidget(close);
    layout->addLayout(buttons);
    connect(refreshButton, &QPushButton::clicked, this, [this] { refresh(); });
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(apply, &QPushButton::clicked, this, [this] {
        // Keep absent radios' reservations. Never apply stale row indices after a rescan.
        const auto current = read_();
        if (current.rows.size() != snapshot_.rows.size() || current.assignments != snapshot_.assignments) {
            status_->setText("Device configuration changed. Refresh before saving."); return;
        }
        auto values = snapshot_.assignments;
        for (size_t i = 0; i < snapshot_.rows.size(); ++i) {
            if (current.rows[i].key != snapshot_.rows[i].key) {
                status_->setText("Device list changed. Refresh before saving."); return;
            }
            if (!snapshot_.rows[i].editable) continue;
            auto* combo = qobject_cast<QComboBox*>(table_->cellWidget(int(i), 1));
            const auto owner = DeviceOwnership::Owner(combo->currentData().toInt());
            const auto key = snapshot_.rows[i].key.toStdString();
            if (owner == DeviceOwnership::Owner::None) values.erase(key);
            else values[key] = owner;
        }
        const auto error = save_(values);
        if (!error.isEmpty()) { status_->setText(error); return; }
        refresh(); status_->setText("Assignments saved");
    });
    refresh();
}

void WorkflowDevicesWindow::refresh() {
    snapshot_ = read_();
    table_->setRowCount(int(snapshot_.rows.size()));
    using O = DeviceOwnership::Owner;
    for (size_t i = 0; i < snapshot_.rows.size(); ++i) {
        const auto& row = snapshot_.rows[i];
        auto* label = new QTableWidgetItem(row.label); label->setToolTip(row.key);
        table_->setItem(int(i), 0, label);
        auto* combo = new QComboBox(table_);
        for (const auto& entry : std::vector<std::pair<QString, O>>{
                 {"Automatic", O::None}, {"Listen", O::Listen}, {"P25 traffic pool", O::P25},
                 {"SSTV", O::Sstv}, {"Inmarsat", O::Inmarsat}, {"Satcom", O::Satcom}, {"Aircraft / 1090", O::Aircraft}})
            combo->addItem(entry.first, int(entry.second));
        auto it = snapshot_.assignments.find(row.key.toStdString());
        combo->setCurrentIndex(combo->findData(int(it == snapshot_.assignments.end() ? O::None : it->second)));
        combo->setEnabled(row.editable);
        table_->setCellWidget(int(i), 1, combo);
        auto* state = new QTableWidgetItem(row.status); state->setToolTip(row.status);
        table_->setItem(int(i), 2, state);
    }
    status_->setText(snapshot_.rows.empty() ? "No radios discovered" : QString());
}
