#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QCheckBox;
class QPlainTextEdit;
class QTimer;
class QTableWidget;
class QShowEvent;
class QHideEvent;

class InmarsatWidget : public QWidget {
    Q_OBJECT
public:
    explicit InmarsatWidget(QWidget* parent = nullptr);
    ~InmarsatWidget() override;

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void onStart();
    void onStop();
    void refreshUi();
    void onBandPlanChanged(int index);
    void onChannelActivated(int row, int column);
    void onVoiceFollowToggled(bool on);
    void onRecordToggled(bool on);

private:
    void buildUi();
    void applyNeonStyle();
    void reloadBandPlans();
    void refreshDevices();

    QComboBox* deviceCombo_ = nullptr;
    QComboBox* planCombo_ = nullptr;
    QTableWidget* channelTable_ = nullptr;
    QPushButton* startBtn_ = nullptr;
    QPushButton* stopBtn_ = nullptr;
    QCheckBox* voiceFollowCheck_ = nullptr;
    QCheckBox* recordCheck_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* lockLabel_ = nullptr;
    QPlainTextEdit* msgView_ = nullptr;
    QTimer* refreshTimer_ = nullptr;
};
