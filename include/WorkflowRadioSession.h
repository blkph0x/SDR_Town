#pragma once
#include "DeviceManager.h"
#include <functional>

// DEC-0181: worker-thread lease for any exclusive RF workflow. Destruction may
// stop a driver; never destroy this on the GUI or an audio callback thread.
class WorkflowRadioSession final {
public:
    WorkflowRadioSession(DeviceManager& manager, const std::string& key,
        DeviceManager::DeviceLeaseOwner owner, double frequencyHz,
        const std::function<bool()>& cancel, double sampleRateHz = 0, double bandwidthHz = 0);
    ~WorkflowRadioSession();
    WorkflowRadioSession(const WorkflowRadioSession&) = delete;
    WorkflowRadioSession& operator=(const WorkflowRadioSession&) = delete;
    size_t deviceIndex() const { return token_.index; }
    bool valid() const;
private:
    DeviceManager& manager_;
    DeviceManager::DeviceLeaseToken token_;
};
