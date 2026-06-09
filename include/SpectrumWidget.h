#pragma once

#include <QWidget>
#include <QVector>
#include <QImage>
#include <QTimer>
#include <QMutex>
#include <complex>
#include <vector>

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

    QImage m_waterfall;             // scrolling image (height = history, width = bins)
    int m_waterfallPos = 0;

    QTimer* m_demoTimer = nullptr;
    QMutex m_dataMutex;

    // interaction
    bool m_dragging = false;
    int m_lastMouseX = 0;
    int m_tuneX = -1;  // last clicked x for visual tune line across full display (incl waterfall)
};