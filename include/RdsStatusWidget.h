#pragma once
#include "RdsMpxDecoder.h"
#include "CtcssDecoder.h"
#include "DcsDecoder.h"
#include <QLabel>

// Presentation only: never holds a receiver DSP lock or changes decoder trust.
class RdsStatusWidget : public QLabel {
public:
    explicit RdsStatusWidget(QWidget* parent = nullptr);
    void present(const RdsMpxSnapshot& snapshot, bool eligible, double targetHz, int64_t nowMs);
    void presentTone(const CtcssSnapshot& snapshot, bool eligible, double targetHz, int64_t nowMs,
                     const DcsSnapshot& dcs = {});
};
