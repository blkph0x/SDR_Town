#include "WorkflowDevicesWindow.h"
#include <catch2/catch_test_macros.hpp>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QApplication>

TEST_CASE("Workflow assignments keep edits until save and preserve absent radios", "[workspace][ownership]") {
    using O = DeviceOwnership::Owner;
    WorkflowDevicesWindow::Snapshot model{{{"a", "Primary", "Main Listen + P25 control", false},
        {"b", "RTL serial 2", "stopped", true}, {"c", "RSPdx", "stopped", true}}, {{"absent", O::Inmarsat}}};
    int saves = 0;
    WorkflowDevicesWindow window([&] { return model; }, [&](const auto& values) {
        ++saves; model.assignments = values; return QString();
    });
    auto* table = window.findChild<QTableWidget*>("workflowDevicesTable");
    REQUIRE(table);
    const auto screenshot = qEnvironmentVariable("SDR_TOWN_WORKFLOW_SCREENSHOT");
    if (!screenshot.isEmpty()) {
        window.show(); QApplication::processEvents();
        REQUIRE(window.grab().save(screenshot));
    }
    CHECK_FALSE(qobject_cast<QComboBox*>(table->cellWidget(0, 1))->isEnabled());
    auto* second = qobject_cast<QComboBox*>(table->cellWidget(1, 1));
    second->setCurrentIndex(second->findData(int(O::Sstv)));
    CHECK(saves == 0);
    window.findChild<QPushButton*>("workflowDevicesSave")->click();
    CHECK(saves == 1);
    CHECK(model.assignments.at("b") == O::Sstv);
    CHECK(model.assignments.at("absent") == O::Inmarsat);
    // A rescan must not apply a combo's row to a different radio.
    model.rows[1].key = "new-device";
    window.findChild<QPushButton*>("workflowDevicesSave")->click();
    CHECK(saves == 1);
    CHECK(window.findChild<QLabel*>("workflowDevicesStatus")->text().contains("Refresh"));
}

TEST_CASE("Workflow save failures stay visible without discarding edits", "[workspace][ownership]") {
    WorkflowDevicesWindow::Snapshot model{{{"a", "RTL", "stopped", true}}, {}};
    WorkflowDevicesWindow window([&] { return model; }, [](const auto&) { return QString("Radio is receiving"); });
    auto* table = window.findChild<QTableWidget*>("workflowDevicesTable");
    auto* combo = qobject_cast<QComboBox*>(table->cellWidget(0, 1));
    combo->setCurrentIndex(combo->findData(int(DeviceOwnership::Owner::P25)));
    window.findChild<QPushButton*>("workflowDevicesSave")->click();
    CHECK(combo->currentData().toInt() == int(DeviceOwnership::Owner::P25));
    CHECK(window.findChild<QLabel*>("workflowDevicesStatus")->text() == "Radio is receiving");
}
