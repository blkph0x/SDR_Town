#include "ClassifierModelBackend.h"

#include <filesystem>

ClassifierModelBackend& ClassifierModelBackend::instance()
{
    static ClassifierModelBackend backend;
    return backend;
}

bool ClassifierModelBackend::loadModel(const std::string& path)
{
    state.modelPath = path;
    state.loaded = false;
    state.enabled = false;

    if (path.empty()) {
        state.message = "No model path supplied.";
        return false;
    }
    if (!std::filesystem::exists(path)) {
        state.message = "Model file does not exist.";
        return false;
    }

#ifdef HAVE_ONNXRUNTIME
    // The project intentionally keeps ONNX Runtime optional. The runtime-specific
    // session object will be added here once the dependency is enabled and a
    // trained model has a frozen input/output contract.
    state.backendName = "onnxruntime";
    state.message = "ONNX Runtime hook compiled, but session binding is not finalized yet.";
    state.loaded = false;
    state.enabled = false;
    return false;
#else
    state.backendName = "deterministic";
    state.message = "ONNX Runtime is not enabled in this build; deterministic classifier remains active.";
    return false;
#endif
}

void ClassifierModelBackend::unloadModel()
{
    state.loaded = false;
    state.enabled = false;
    state.modelPath.clear();
    state.backendName = "deterministic";
    state.message = "Model unloaded; deterministic classifier active.";
}

ClassifierModelStatus ClassifierModelBackend::status() const
{
    return state;
}

std::optional<SignalRecommendation> ClassifierModelBackend::classifyTile(const ClassifierTile&,
                                                                        double,
                                                                        double,
                                                                        double,
                                                                        double) const
{
    if (!state.enabled || !state.loaded) return std::nullopt;
    return std::nullopt;
}
