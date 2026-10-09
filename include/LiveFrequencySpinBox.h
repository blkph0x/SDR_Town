#pragma once
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QWheelEvent>
#include <functional>

class LiveFrequencySpinBox : public QDoubleSpinBox {
public:
    explicit LiveFrequencySpinBox(QWidget* parent=nullptr) : QDoubleSpinBox(parent) {
        lineEdit()->installEventFilter(this);
        setToolTip("Mouse wheel tunes live. Ctrl: finer steps. Shift: larger steps.");
    }
    std::function<void(double)> onWheelTune;
protected:
    bool eventFilter(QObject* object,QEvent* event) override {
        if(object==lineEdit() && event->type()==QEvent::Wheel){wheelEvent(static_cast<QWheelEvent*>(event));return true;}
        return QDoubleSpinBox::eventFilter(object,event);
    }
    void wheelEvent(QWheelEvent* event) override {
        const int delta=event->angleDelta().y();
        if(!delta){event->ignore();return;}
        wheelRemainder+=delta;
        const int steps=wheelRemainder/120; wheelRemainder-=steps*120;
        if(steps){
            const double old=value();
            double step=singleStep();
            if(event->modifiers().testFlag(Qt::ControlModifier))step/=10;
            else if(event->modifiers().testFlag(Qt::ShiftModifier))step*=10;
            setValue(old+steps*step);
            if(value()!=old && onWheelTune)onWheelTune(value());
        }
        event->accept();
    }
private:
    int wheelRemainder=0;
};
