#pragma once
#include <QDialog>
#include <functional>

class BandPlanDialog final : public QDialog {
public:
    explicit BandPlanDialog(std::function<void()> applied, QWidget* parent = nullptr);
};
