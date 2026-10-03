#pragma once
#include "CwSession.h"
#include <QDialog>
#include <memory>
#include <thread>
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QLabel;
class QPlainTextEdit;

class CwWindow final : public QDialog {
    Q_OBJECT
public:
    using LiveFactory = std::function<CwRun()>;
    explicit CwWindow(LiveFactory live, QWidget* parent = nullptr);
    ~CwWindow() override;
protected:
    void closeEvent(QCloseEvent* event) override;
    void hideEvent(QHideEvent* event) override;
private:
    void start();
    void refresh();
    void setBusy(bool busy);
    struct Work;
    LiveFactory live_;
    std::shared_ptr<Work> work_;
    std::thread worker_;
    bool closePending_ = false;
    QComboBox* source_;
    QDoubleSpinBox *pitch_, *speed_;
    QLineEdit* file_;
    QPushButton *open_, *start_, *stop_, *clear_;
    QLabel *status_, *details_, *origin_;
    QPlainTextEdit* text_;
};
