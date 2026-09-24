#pragma once
#include "InmarsatReplay.h"
#include <QDialog>
#include <functional>
class QLineEdit;
class QComboBox;
class QDoubleSpinBox;
class QCheckBox;
class QSlider;
class QLabel;
class QToolButton;
class QPlainTextEdit;
class InmarsatMapWidget;

class InmarsatReplayDialog : public QDialog {
public:
    explicit InmarsatReplayDialog(QWidget* parent = nullptr);
    ~InmarsatReplayDialog() override;
    void startReplay(const InmarsatReplayOptions& options);
    void stopReplay() { replay_.stop(); }
    void setSharingControl(bool enabled, std::function<bool()> enable);
    InmarsatReplaySnapshot snapshot() const { return replay_.snapshot(); }
private:
    void refresh();
    void play();
    InmarsatReplay replay_;
    QLineEdit* file_;
    QComboBox *format_, *mode_;
    QDoubleSpinBox *rate_, *center_, *channel_;
    QCheckBox* realTime_;
    QCheckBox* playAudio_;
    QLineEdit* wav_;
    InmarsatMapWidget* map_;
    QCheckBox* share_;
    std::function<bool()> enableSharing_;
    QSlider* position_;
    QLabel *time_, *status_, *log_;
    QToolButton *play_, *pause_, *stop_, *open_;
    QPlainTextEdit* details_;
};
