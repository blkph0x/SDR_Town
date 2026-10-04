#pragma once
#include "DeviceManager.h"
#include <QComboBox>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QAbstractItemView>
#include <algorithm>

// DEC-0182: item data is a stable identity, never an enumeration index. Keep a
// missing selection visible; refresh must not silently select another radio.
inline void refreshWorkflowDeviceCombo(QComboBox& combo, DeviceOwnership::Owner owner,
    const QString& selectedKey) {
    if (combo.view()->isVisible()) return;
    auto& manager = DeviceManager::instance();
    struct Item { QString label, key; bool enabled; };
    std::vector<Item> items{{"Automatic (workflow assignment)", {}, true}};
    const auto devices = manager.getDevices();
    for (size_t i = 0; i < devices.size(); ++i) {
        const auto& device = devices[i];
        std::string reason;
        const bool available = manager.canUseDevice(i, owner, &reason);
        auto label = QString::fromStdString(device.label);
        if (!available) label += " [" + QString::fromStdString(reason) + "]";
        items.push_back({label, QString::fromStdString(device.stableKey), available});
    }
    if (std::none_of(items.begin(), items.end(), [&](const auto& item) { return item.key == selectedKey; }))
        items.push_back({"Unavailable: " + selectedKey, selectedKey, false});
    bool changed = combo.count() != static_cast<int>(items.size());
    for (int i = 0; !changed && i < combo.count(); ++i)
        changed = combo.itemText(i) != items[i].label || combo.itemData(i).toString() != items[i].key;
    const QSignalBlocker blocker(&combo);
    if (changed) {
        combo.clear();
        for (const auto& item : items) {
            combo.addItem(item.label, item.key);
            combo.setItemData(combo.count() - 1, item.label, Qt::ToolTipRole);
            if (auto* model = qobject_cast<QStandardItemModel*>(combo.model())) model->item(combo.count() - 1)->setEnabled(item.enabled);
        }
    }
    combo.setCurrentIndex(combo.findData(selectedKey));
}
