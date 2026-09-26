#pragma once
#include <QDialog>
#include "AntennaControl.h"
class QMainWindow;
class QLabel;
class QCheckBox;
class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;
class QPlainTextEdit;
class AntennaControlWindow : public QDialog {
public:
    explicit AntennaControlWindow(QWidget* parent=nullptr);
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    void save();
    void log(const QString& message);
    RotatorController rotor_;
    SwrMonitor meter_;
    QLineEdit *host_,*meterHost_;
    QSpinBox *port_,*meterPort_;
    QDoubleSpinBox *az_,*el_,*minAz_,*maxAz_,*minEl_,*maxEl_,*parkAz_,*parkEl_;
    QCheckBox* arm_;
    QLabel *position_,*state_,*swr_;
    QPlainTextEdit* history_;
};
void installAntennaControlMenu(QMainWindow& window);
