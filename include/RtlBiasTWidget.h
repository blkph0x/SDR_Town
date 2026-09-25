#pragma once
#include "RtlBiasT.h"
#include <QWidget>
#include <functional>
class QCheckBox;
class QLabel;

class RtlBiasTWidget : public QWidget {
public:
    explicit RtlBiasTWidget(QWidget* parent = nullptr);
    void setState(const RtlBiasT::State& state);
    std::function<bool()> confirmPower;
    std::function<bool(bool, std::string&)> apply;
private:
    RtlBiasT::State state_;
    QCheckBox* toggle_;
    QLabel* status_;
};
