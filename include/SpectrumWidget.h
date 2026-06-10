#pragma once

#include <QWidget>
#include <QVector>
#include <QImage>
#include <QTimer>
#include <QMutex>
#include <complex>
#include <vector>
#include <deque>

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

signals:
    void frequencySelected(double freqHz);  // user clicked
    void bandwidthSelected(double bwHz);    // future drag select

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

    // SOTA high-resolution spectrum history for *true* zoomed waterfall (not crop/stretch of low-res image).
    // Each row is a full high-bin FFT power vector (e.g. 8192). Paint uses sub-range bin lookup for the current viewBw.
    // This delivers the extra resolution the user asked for when zooming the waterfall.
    std::deque<std::vector<float>> m_highResHistory;
    static constexpr size_t kMaxHighResHistory = 256;

    QTimer* m_demoTimer = nullptr;
    QMutex m_dataMutex;

    // interaction
    bool m_dragging = false;
    int m_lastMouseX = 0;
    int m_tuneX = -1;  // last clicked x for visual tune line across full display (incl waterfall)
};