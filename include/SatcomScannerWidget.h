#pragma once

#include <QString>
#include <QWidget>
#include <QJsonObject>
#include <functional>
#include <memory>

class SpectrumWidget;
class QDoubleSpinBox;
class QComboBox;
class QSlider;
class QLabel;
class QPushButton;
class QTimer;
class QPlainTextEdit;
class QLineEdit;
class QTableWidget;
class QCheckBox;
class QSpinBox;
class QTabWidget;
class QShowEvent;
class QHideEvent;
class ObserverMapWidget;
struct SatPassPlannerSnapshot;
class SatcomScannerEngine;
struct SatcomScannerConfig;
struct SatcomScannerSnapshot;
struct SatcomWatchChannel;

class SatcomScannerWidget : public QWidget {
    Q_OBJECT
public:
    explicit SatcomScannerWidget(QWidget* parent = nullptr, const QString& sessionId = {});
    ~SatcomScannerWidget() override;
    SatcomScannerEngine& engine() { return engine_; }
    void reloadSessionControls();
    void stopSession() { onStop(); }
    void autoCaptureTick();
    QJsonObject webStatus() const;
    QJsonObject webPassControl(const QJsonObject& body);

signals:
    void requestOpenSstvLive();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onStart();
    void onStop();
    void onSkip();
    void onRecord();
    void refreshUi();
    bool applyFieldsToConfig();
    void onSelectSats();
    void onRefreshTle();
    void onApplyObserver();
    void onArmSelected();
    void onDisarm();
    void onArmSstv();
    void onEditSstvProfile();
    void saveRange();
    void deleteRange();
    void draftChanged();
    void addWatchChannel();
    void removeWatchChannel();

private:
    std::unique_ptr<SatcomScannerEngine> ownedEngine_;
    SatcomScannerEngine& engine_;
    void buildUi();
    void refreshPassesTable();
    void updateAutoCapture(const SatPassPlannerSnapshot& plan);
    void stopAutoCapture(bool keepHandledKey);
    SatcomScannerConfig fieldsConfig() const;
    void refreshRangeChoices();
    void updateRangeSummary();
    void refreshChannels(const SatcomScannerSnapshot& snapshot);
    bool editWatchChannel(double frequencyHz, const std::function<void(SatcomWatchChannel&)>& edit);

    QTabWidget* taskTabs_ = nullptr;
    QWidget* channelsPage_ = nullptr;
    QTableWidget* channelsTable_ = nullptr;
    QDoubleSpinBox* channelFrequencySpin_ = nullptr;
    QPushButton* addChannelBtn_ = nullptr;
    QPushButton* removeChannelBtn_ = nullptr;
    QLabel* channelsSummary_ = nullptr;
    QLabel* channelError_ = nullptr;
    QWidget* scanSettings_ = nullptr;
    QSpinBox* dwellSpin_ = nullptr;
    QCheckBox* smartScanCheck_ = nullptr;
    QCheckBox* autoSquelchCheck_ = nullptr;
    QDoubleSpinBox* squelchMarginSpin_ = nullptr;
    QSpinBox* quietHoldSpin_ = nullptr;
    QSlider* smartVolumeSlider_ = nullptr;
    QLabel* smartVolumeValue_ = nullptr;
    QLabel* rangeSummary_ = nullptr;
    QLabel* rangeNote_ = nullptr;
    QLabel* spectrumEmpty_ = nullptr;
    QPushButton* saveRangeBtn_ = nullptr;
    QPushButton* deleteRangeBtn_ = nullptr;
    bool draftDirty_ = false;
    SpectrumWidget* spectrum_ = nullptr;
    QDoubleSpinBox* lowSpin_ = nullptr;
    QDoubleSpinBox* highSpin_ = nullptr;
    QDoubleSpinBox* stepSpin_ = nullptr;
    QDoubleSpinBox* bwSpin_ = nullptr;
    QComboBox* modeCombo_ = nullptr;
    QComboBox* deviceCombo_ = nullptr;
    QComboBox* presetCombo_ = nullptr;
    QSlider* squelchSlider_ = nullptr;
    QDoubleSpinBox* squelchSpin_ = nullptr;
    QCheckBox* monitorAudioCheck_ = nullptr;
    QPushButton* startBtn_ = nullptr;
    QPushButton* skipBtn_ = nullptr;
    QPushButton* recordBtn_ = nullptr;
    QPushButton* stopBtn_ = nullptr;
    QLabel* statusDevice_ = nullptr;
    QLabel* statusScan_ = nullptr;
    QLabel* statusRec_ = nullptr;
    QLabel* statusHealth_ = nullptr;
    QLabel* audioMeter_ = nullptr;
    QPlainTextEdit* logView_ = nullptr;
    QTimer* refreshTimer_ = nullptr;
    bool recordingUi_ = false;
    int passTableThrottle_ = 0;

    QLineEdit* latEdit_ = nullptr;
    QLineEdit* lonEdit_ = nullptr;
    QDoubleSpinBox* altSpin_ = nullptr;
    QDoubleSpinBox* minElSpin_ = nullptr;
    QLabel* tleAgeLabel_ = nullptr;
    QLabel* passStatusLabel_ = nullptr;
    QTableWidget* passTable_ = nullptr;
    QCheckBox* autoTrackCheck_ = nullptr;
    QCheckBox* autoCaptureCheck_ = nullptr;
    QCheckBox* sstvOnlyCheck_ = nullptr;
    QComboBox* downlinkCombo_ = nullptr;
    ObserverMapWidget* observerMap_ = nullptr;
    QPushButton* refreshTleBtn_ = nullptr;
    bool tleBusy_ = false;

    QString autoCapturePassKey_;
    qint64 autoCaptureRetryAfter_ = 0;
    bool autoCaptureOwned_ = false;
    bool autoEngineWasRunning_ = false;
    bool autoSstvRequested_ = false;
};
