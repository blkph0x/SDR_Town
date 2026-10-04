#pragma once
#include "Receiver.h"
#include <QComboBox>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QAbstractItemView>
#include <functional>
#include <algorithm>
#include <iterator>

// Read-only logical-receiver selection, shared by side decoders. IDs are local
// to this window and never recycled; removing a receiver cannot select its
// replacement by vector index. This class neither tunes nor starts hardware.
class ReceiverSourcePicker final : public QComboBox {
public:
    using Snapshot = std::function<std::vector<std::shared_ptr<Receiver>>() >;
    explicit ReceiverSourcePicker(Snapshot snapshot, QWidget* parent = nullptr)
        : QComboBox(parent), snapshot_(std::move(snapshot)) {
        setObjectName("receiverSource");
        setSizeAdjustPolicy(AdjustToMinimumContentsLengthWithIcon);
        setMinimumContentsLength(24);
        refresh();
    }
    std::shared_ptr<Receiver> selected() const {
        const int id = currentData().toInt();
        if (id < 1 || id > static_cast<int>(known_.size())) return {};
        const auto receiver = known_[id - 1].lock();
        const auto live = snapshot_();
        return receiver && std::find(live.begin(), live.end(), receiver) != live.end() ? receiver : nullptr;
    }
    void refresh() {
        const auto live = snapshot_();
        const int old = currentData().toInt();
        const QSignalBlocker blocker(this);
        clear();
        for (const auto& receiver : live) {
            if (!receiver) continue;
            auto it = std::find_if(known_.begin(), known_.end(), [&](const auto& p) { return p.lock() == receiver; });
            if (it == known_.end()) { known_.push_back(receiver); it = std::prev(known_.end()); }
            const int id = static_cast<int>(std::distance(known_.begin(), it)) + 1;
            std::unique_lock lock(receiver->stateMutex, std::try_to_lock);
            const auto label = !lock.owns_lock() ? QString("Receiver %1 (busy)").arg(id) :
                QString("Receiver %1 | Radio %2 | %3 MHz%4").arg(id).arg(receiver->deviceIndex)
                    .arg(receiver->freqHz / 1e6, 0, 'f', 5).arg(receiver->active ? "" : " | stopped");
            addItem(label, id);
        }
        int row = findData(old);
        if (old > 0 && row < 0) { addItem(QString("Receiver %1 unavailable").arg(old), old); row = count() - 1; }
        if (count() == 0) addItem("No receiver available", 0);
        setCurrentIndex(row < 0 ? 0 : row);
    }
protected:
    void showPopup() override { refresh(); QComboBox::showPopup(); }
private:
    Snapshot snapshot_;
    std::vector<std::weak_ptr<Receiver>> known_;
};
