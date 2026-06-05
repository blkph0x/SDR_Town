#include "SpectrumWidget.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QPaintEvent>
#include <cmath>
#include <algorithm>

SpectrumWidget::SpectrumWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(220);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    // demo data timer (will be replaced by real pipeline)
    m_demoTimer = new QTimer(this);
    connect(m_demoTimer, &QTimer::timeout, this, [this]() {
        computeFakeSpectrum();
    });
    m_demoTimer->start(80); // ~12 fps demo

    // initial waterfall
    m_waterfall = QImage(512, 180, QImage::Format_RGB32);
    m_waterfall.fill(Qt::black);
}

SpectrumWidget::~SpectrumWidget() = default;

void SpectrumWidget::updateSpectrum(const std::vector<float>& powerDb, double centerFreqHz, double sampleRateHz)
{
    QMutexLocker lock(&m_dataMutex);
    m_powerDb = QVector<float>(powerDb.begin(), powerDb.end());
    m_centerFreq = centerFreqHz;
    m_sampleRate = sampleRateHz;
    scrollWaterfall(powerDb);
    update();
}

void SpectrumWidget::updateIQ(const std::vector<std::complex<float>>& iq, double centerFreqHz, double sampleRateHz)
{
    // TODO PR3: real FFT here (use liquid-dsp fft or fftw/kiss once wired)
    // For now convert to fake power
    std::vector<float> pwr(iq.size());
    for (size_t i = 0; i < iq.size(); ++i) {
        float mag = std::abs(iq[i]);
        pwr[i] = 20.0f * std::log10(std::max(mag, 1e-6f));
    }
    updateSpectrum(pwr, centerFreqHz, sampleRateHz);
}

void SpectrumWidget::setCenterFreq(double hz) { m_centerFreq = hz; update(); }
void SpectrumWidget::setSampleRate(double hz) { m_sampleRate = hz; update(); }

void SpectrumWidget::setFreqRange(double minHz, double maxHz)
{
    m_minFreq = minHz;
    m_maxFreq = maxHz;
    update();
}

void SpectrumWidget::computeFakeSpectrum()
{
    const int bins = 512;
    std::vector<float> pwr(bins);
    double t = QDateTime::currentMSecsSinceEpoch() / 150.0;

    // nice fake spectrum with a few carriers + noise floor + one drifting
    for (int i = 0; i < bins; ++i) {
        double x = (i - bins/2) / double(bins) * 1.0; // normalized
        double noise = -95.0 + (rand() % 40 - 20) * 0.1;

        // peaks
        double p1 = -40 + 30 * std::exp(-((x-0.12)*(x-0.12)) / 0.002);
        double p2 = -55 + 25 * std::exp(-((x+0.25)*(x+0.25)) / 0.003);
        double p3 = -30 + 35 * std::exp(-((x-0.02 + 0.01*std::sin(t*0.8))*(x-0.02 + 0.01*std::sin(t*0.8))) / 0.0008); // drifting
        double p4 = -65 + 15 * std::exp(-((x+0.45)*(x+0.45)) / 0.01);

        pwr[i] = std::max({noise, p1, p2, p3, p4});
    }

    updateSpectrum(pwr, m_centerFreq, m_sampleRate);
}

void SpectrumWidget::scrollWaterfall(const std::vector<float>& latestPower)
{
    if (m_waterfall.isNull()) return;

    // simple color map: dB -> color (blue low, green, yellow, red high)
    auto dbToColor = [](float db) -> QRgb {
        float norm = std::clamp((db + 100.0f) / 70.0f, 0.0f, 1.0f); // -100..-30 dB
        int r = std::clamp(int(255 * (norm * 1.2 - 0.2)), 0, 255);
        int g = std::clamp(int(255 * std::sin(norm * 3.14)), 0, 255);
        int b = std::clamp(int(255 * (1.0 - norm)), 40, 255);
        return qRgb(r, g, b);
    };

    int w = m_waterfall.width();
    int h = m_waterfall.height();

    // scroll down (new line at top)
    for (int y = h-1; y > 0; --y) {
        memcpy(m_waterfall.scanLine(y), m_waterfall.scanLine(y-1), w * 4);
    }

    // draw newest line at y=0 (scaled to width)
    int bins = static_cast<int>(latestPower.size());
    for (int x = 0; x < w; ++x) {
        int src = bins * x / w;
        float db = (src < bins) ? latestPower[src] : -100.0f;
        m_waterfall.setPixel(x, 0, dbToColor(db));
    }

    m_waterfallPos = (m_waterfallPos + 1) % h;
}

double SpectrumWidget::freqFromX(int x) const
{
    double bw = m_sampleRate;
    double start = m_centerFreq - bw/2;
    return start + (x / double(width())) * bw;
}

int SpectrumWidget::xFromFreq(double freq) const
{
    double bw = m_sampleRate;
    double start = m_centerFreq - bw/2;
    double rel = (freq - start) / bw;
    return static_cast<int>(rel * width());
}

void SpectrumWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    int w = width();
    int h = height();
    int specH = h * 2 / 3;
    int wfH = h - specH;

    // background
    p.fillRect(rect(), QColor(20, 22, 25));

    // spectrum area
    p.setPen(QColor(60, 65, 70));
    p.drawRect(0, 0, w-1, specH-1);

    // grid + labels
    p.setPen(QColor(70, 75, 80));
    for (int i = 0; i <= 10; ++i) {
        int x = w * i / 10;
        p.drawLine(x, 0, x, specH);
        double f = freqFromX(x);
        p.setPen(QColor(140, 145, 150));
        p.drawText(x + 2, specH - 4, QString::number(f/1e6, 'f', 2) + "M");
        p.setPen(QColor(70, 75, 80));
    }

    // draw spectrum
    QMutexLocker lock(&m_dataMutex);
    if (!m_powerDb.isEmpty()) {
        QPolygonF poly;
        int n = m_powerDb.size();
        for (int i = 0; i < n; ++i) {
            int x = w * i / n;
            float db = m_powerDb[i];
            // map -120 .. -20 dB to specH
            float norm = std::clamp((db + 120.0f) / 100.0f, 0.0f, 1.0f);
            int y = specH - 5 - static_cast<int>(norm * (specH - 10));
            poly << QPointF(x, y);
        }

        // filled
        QPolygonF fillPoly = poly;
        fillPoly << QPointF(w, specH) << QPointF(0, specH);
        p.setBrush(QColor(30, 120, 180, 60));
        p.setPen(Qt::NoPen);
        p.drawPolygon(fillPoly);

        // line
        p.setPen(QPen(QColor(100, 200, 255), 1.5));
        p.drawPolyline(poly);

        // peak hold simple (max line)
        p.setPen(QPen(QColor(255, 220, 100, 180), 1));
        // (omitted full peak for brevity - would store max)
    } else {
        p.setPen(QColor(100, 180, 255));
        p.drawText(w/2 - 60, specH/2, "No spectrum data (waiting for IQ...)");
    }

    // waterfall
    if (!m_waterfall.isNull()) {
        QRect wfRect(0, specH, w, wfH);
        p.drawImage(wfRect, m_waterfall);
        p.setPen(QColor(80, 85, 90));
        p.drawRect(wfRect.adjusted(0,0,-1,-1));
    }

    // center marker
    int cx = w / 2;
    p.setPen(QPen(QColor(255, 80, 80), 1, Qt::DashLine));
    p.drawLine(cx, 0, cx, specH);

    // title / info
    p.setPen(Qt::white);
    p.drawText(8, 16, QString("Center: %1 MHz   SR: %2 MS/s   (click to tune)")
                          .arg(m_centerFreq / 1e6, 0, 'f', 3)
                          .arg(m_sampleRate / 1e6, 0, 'f', 2));
}

void SpectrumWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        double f = freqFromX(event->pos().x());
        emit frequencySelected(f);
        m_dragging = true;
        m_lastMouseX = event->pos().x();
        update();
    }
}

void SpectrumWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging) {
        double f = freqFromX(event->pos().x());
        emit frequencySelected(f);
        update();
    }
}

void SpectrumWidget::wheelEvent(QWheelEvent* event)
{
    // simple zoom simulation by changing sample rate (display bw)
    double factor = (event->angleDelta().y() > 0) ? 0.8 : 1.25;
    m_sampleRate = std::clamp(m_sampleRate * factor, 0.1e6, 30e6);
    update();
    event->accept();
}

void SpectrumWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    // resize waterfall keeping history aspect
    if (width() > 0) {
        int newW = width();
        int newH = m_waterfall.height();
        if (newW != m_waterfall.width()) {
            QImage newWf(newW, newH, QImage::Format_RGB32);
            newWf.fill(Qt::black);
            // simple stretch copy (good enough for now)
            for (int y = 0; y < newH; ++y) {
                for (int x = 0; x < newW; ++x) {
                    int sx = x * m_waterfall.width() / newW;
                    newWf.setPixel(x, y, m_waterfall.pixel(sx, y));
                }
            }
            m_waterfall = newWf;
        }
    }
}