#pragma once

#include <QWidget>
#include <nlohmann/json.hpp>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QCheckBox;
class QPlainTextEdit;
class QTimer;
class QTableWidget;
class QShowEvent;
class QHideEvent;
class InmarsatMapWidget;
class InmarsatWatchSpectrum;
class InmarsatConstellationWidget;
class QSpinBox;
class QLineEdit;

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
    void refreshVisuals();
    void onBandPlanChanged(int index);
    void onChannelActivated(int row, int column);
    void onVoiceFollowToggled(bool on);
    void onRecordToggled(bool on);

private:
    void buildUi();
    void applyNeonStyle();
    void reloadBandPlans();
    void refreshDevices();
    void populateChannels();
    void syncTuningControls();
    bool applyTuningControls();
    QWidget* buildWatchUi();
    void reloadWatchUi();
    void saveWatchPolicy();
    void updateWatchUi(bool running, const nlohmann::json& report);
    void addWatchChannel();

    QComboBox* deviceCombo_ = nullptr;
    QComboBox* planCombo_ = nullptr;
    QComboBox* decoderCombo_ = nullptr;
    QDoubleSpinBox* frequency_ = nullptr;
    QTableWidget* channelTable_ = nullptr;
    QPushButton* startBtn_ = nullptr;
    QPushButton* stopBtn_ = nullptr;
    QCheckBox* voiceFollowCheck_ = nullptr;
    QCheckBox* recordCheck_ = nullptr;
    QCheckBox* speakerCheck_ = nullptr;
    InmarsatMapWidget* map_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* lockLabel_ = nullptr;
    QLabel* audioLabel_ = nullptr;
    QLabel* presetHint_ = nullptr;
    QPlainTextEdit* msgView_ = nullptr;
    QTimer* refreshTimer_ = nullptr;
    QTimer* visualTimer_ = nullptr;
    QCheckBox* clickAdd_ = nullptr;
    QCheckBox* simultaneousWatch_ = nullptr;
    QComboBox* constellationChannel_ = nullptr;
    InmarsatConstellationWidget* constellation_ = nullptr;
    InmarsatWatchSpectrum* watchSpectrum_ = nullptr;
    QTableWidget* watchTable_ = nullptr;
    QLineEdit* watchName_ = nullptr;
    QWidget* watchEditors_ = nullptr;
    QLabel* watchStatus_ = nullptr;
    QSpinBox *dataMin_=nullptr,*dataDwell_=nullptr,*positionTarget_=nullptr,
        *voiceAcquire_=nullptr,*voiceIdle_=nullptr,*refreshInterval_=nullptr,*maxVoice_=nullptr,*watchConcurrent_=nullptr;
};
