#pragma once

#include <QWidget>

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
class QShowEvent;
class QHideEvent;
class ObserverMapWidget;

class SatcomScannerWidget : public QWidget {
    Q_OBJECT
public:
    explicit SatcomScannerWidget(QWidget* parent = nullptr);
    ~SatcomScannerWidget() override;

signals:
    void requestOpenSstvLive();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void onStart();
    void onStop();
    void onSkip();
    void onRecord();
    void refreshUi();
    void applyFieldsToConfig();
    void onSelectSats();
    void onRefreshTle();
    void onApplyObserver();
    void onArmSelected();
    void onDisarm();
    void onArmSstv();

private:
    void buildUi();
    void applyNeonStyle();
    void refreshPassesTable();

    SpectrumWidget* spectrum_ = nullptr;
    QDoubleSpinBox* lowSpin_ = nullptr;
    QDoubleSpinBox* highSpin_ = nullptr;
    QDoubleSpinBox* stepSpin_ = nullptr;
    QDoubleSpinBox* bwSpin_ = nullptr;
    QComboBox* modeCombo_ = nullptr;
    QComboBox* presetCombo_ = nullptr;
    QSlider* squelchSlider_ = nullptr;
    QDoubleSpinBox* squelchSpin_ = nullptr;
    QPushButton* startBtn_ = nullptr;
    QPushButton* skipBtn_ = nullptr;
    QPushButton* recordBtn_ = nullptr;
    QPushButton* stopBtn_ = nullptr;
    QLabel* statusDevice_ = nullptr;
    QLabel* statusScan_ = nullptr;
    QLabel* statusRec_ = nullptr;
    QLabel* audioMeter_ = nullptr;
    QPlainTextEdit* logView_ = nullptr;
    QTimer* refreshTimer_ = nullptr;
    bool recordingUi_ = false;

    QLineEdit* latEdit_ = nullptr;
    QLineEdit* lonEdit_ = nullptr;
    QDoubleSpinBox* altSpin_ = nullptr;
    QDoubleSpinBox* minElSpin_ = nullptr;
    QLabel* tleAgeLabel_ = nullptr;
    QLabel* passStatusLabel_ = nullptr;
    QTableWidget* passTable_ = nullptr;
    QCheckBox* autoTrackCheck_ = nullptr;
    QComboBox* downlinkCombo_ = nullptr;
    ObserverMapWidget* observerMap_ = nullptr;
};
