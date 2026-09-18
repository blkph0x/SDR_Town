#pragma once

#include <QWidget>
#include <QVector>
#include <QImage>
#include <QTimer>
#include <QMutex>
#include <complex>
#include <vector>
#include <deque>
#include "BandPlan.h"

class SpectrumWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SpectrumWidget(QWidget* parent = nullptr);
    ~SpectrumWidget() override;

    // Feed new IQ or power spectrum data (thread safe)
    void updateSpectrum(const std::vector<float>& powerDb, double centerFreqHz, double sampleRateHz);
    void updateIQ(const std::vector<std::complex<float>>& iq, double centerFreqHz, double sampleRateHz);

    // Control
    void setCenterFreq(double hz);
    void setSampleRate(double hz);
    void setFreqRange(double minHz, double maxHz); // for display zoom/pan

    double centerFreq() const { return m_centerFreq; }
    double sampleRate() const { return m_sampleRate; }

    // Heat map / color range for waterfall and spectrum (user adjustable noise floor / sensitivity display)
    void setColorRange(double minDb, double maxDb); // e.g. -120 to -10
    void setViewBandwidth(double bwHz); // for zooming the display (independent of device SR for visual fine tuning)

    // Interactive squelch threshold (dB) visual + control.
    // The line + right grab bar use the same dB axis as the spectrum so SQ, signal, and noise floor align visually.
    void setSquelchThreshold(double db);

    // Live RF signal/noise markers in the same dB units as the spectrum and squelch line.
    // Called from the main level timer so the plot can show the current squelch metric and floor.
    void setLiveRms(double rmsDb);
    void setLiveLevels(double signalDb, double noiseFloorDb);
    void setBandPlanMonitorFrequency(double hz); // GUI-thread overlay target, not hardware LO
    void setBandPlanOverlayEnabled(bool enabled);

signals:
    void frequencySelected(double freqHz);  // click/drag committed on release
    void bandwidthSelected(double bwHz);    // future drag select
    void squelchThresholdChanged(double db); // user dragged the interactive squelch line/bar on the right side

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override; // fine tuning arrows + zoom in/out on widget

private:
    void computeFakeSpectrum(); // for demo when no real data
    void scrollWaterfall(const std::vector<float>& latestPower);
    double freqFromX(int x) const;
    int xFromFreq(double freq) const;

    // Helpers for the new left dB axis + interactive squelch line positioning.
    int yFromDb(double db, int specH) const;
    double dbFromY(int y, int specH) const;

    // Legacy fixed scale helpers kept for compatibility with older call sites/tests.
    int yFromSquelchViz(double db, int specH) const;
    double squelchVizDbFromY(int y, int specH) const;

    QVector<float> m_powerDb;       // current spectrum (dB, size = fft size)
    double m_centerFreq = 100e6;
    double m_sampleRate = 2.4e6;
    double m_minFreq = 0;
    double m_maxFreq = 0;

    // Display view for zoom/pan (higher effective resolution when zoomed in)
    double m_viewBandwidthHz = 0;   // 0 = use full m_sampleRate

    // Adjustable heat map range (controls noise floor visibility in waterfall/spectrum colors)
    double m_colorMinDb = -120.0;
    double m_colorMaxDb = -10.0;

    QImage m_waterfall;             // scrolling image (height = history, width = bins) - used for fast full-view path
    int m_waterfallPos = 0;

    // High-resolution spectrum history for waterfall rendering.
    // Each row is a full high-bin FFT power vector. Paint uses sub-range bin lookup
    // for the current view, so the spectrum and waterfall share one frequency axis.
    std::deque<std::vector<float>> m_highResHistory;
    // Cap history: long FM sessions + 256×FFT-row paint on the UI thread froze
    // the GUI after ~10–15 min (field 20260811). Keep enough for zoomed detail.
    static constexpr size_t kMaxHighResHistory = 96;

    QTimer* m_demoTimer = nullptr;
    QMutex m_dataMutex;

    // interaction
    bool m_dragging = false;
    int m_lastMouseX = 0;
    double m_dragLowHz = 0, m_dragBwHz = 0, m_dragPreviewHz = 0;
    int m_dragPlotWidth = 1;
    int m_tuneX = -1;  // last clicked x for visual tune line across full display (incl waterfall)

    // Squelch visualization + interactive control (linked to main GUI Squelch spin + receivers)
    double m_squelchThresholdDb = -80.0;
    bool m_squelchDragging = false;
    double m_bandPlanMonitorHz = 0;
    bool m_bandPlanOverlayEnabled = true;
    std::shared_ptr<const BandPlanProfile> m_overlayProfile;
    double m_overlayLow = 0, m_overlayHigh = 0;
    std::vector<BandPlanEntry> m_overlaySections;

    // Live RF markers for drawing "signal" and "noise floor" reference lines.
    double m_liveSignalDb = -100.0;
    double m_liveNoiseFloorDb = -120.0;
};
