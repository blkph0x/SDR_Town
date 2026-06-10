#include "SpectrumWidget.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QDateTime>
#include <cmath>
#include <algorithm>

SpectrumWidget::SpectrumWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(220);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    // initial waterfall (higher horizontal res for improved detail in heat map / zoomed view)
    m_waterfall = QImage(1024, 200, QImage::Format_RGB32);
    m_waterfall.fill(Qt::black);

    // demo data timer (will be replaced by real pipeline)
    // IMPORTANT: do NOT start() synchronously in ctor. Starting the timer (and thus
    // computeFakeSpectrum + update + paint) during MainWindow construction / layout
    // can lead to blank widget or crashes on first paint before the window is shown
    // and sized. We defer the actual start until shortly after construction.
    m_demoTimer = new QTimer(this);
    connect(m_demoTimer, &QTimer::timeout, this, [this]() {
        computeFakeSpectrum();
    });
    // Defer start so first fake spectrum/paint happens after event loop + show + layout.
    QTimer::singleShot(60, this, [this]() {
        if (m_demoTimer && !m_demoTimer->isActive()) {
            m_demoTimer->start(80); // ~12 fps demo
        }
    });
}

SpectrumWidget::~SpectrumWidget() = default;

void SpectrumWidget::updateSpectrum(const std::vector<float>& powerDb, double centerFreqHz, double sampleRateHz)
{
    QMutexLocker lock(&m_dataMutex);
    m_powerDb = QVector<float>(powerDb.begin(), powerDb.end());
    m_centerFreq = centerFreqHz;
    m_sampleRate = sampleRateHz;
    if (m_viewBandwidthHz <= 0) m_viewBandwidthHz = sampleRateHz;

    // Push full high-res row to history (source of truth for zoomed render).
    if (!powerDb.empty()) {
        m_highResHistory.push_back(powerDb);
        while (m_highResHistory.size() > kMaxHighResHistory) m_highResHistory.pop_front();
    }

    scrollWaterfall(powerDb); // keep image build for fast full-view / compat
    if (powerDb.size() >= 256 && m_demoTimer) {
        m_demoTimer->stop();
        m_demoTimer->deleteLater();
        m_demoTimer = nullptr;
    }
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

void SpectrumWidget::setColorRange(double minDb, double maxDb)
{
    m_colorMinDb = minDb;
    m_colorMaxDb = maxDb;
    update();
}

void SpectrumWidget::setViewBandwidth(double bwHz)
{
    m_viewBandwidthHz = std::max(1000.0, bwHz);
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

    // Improved heat map / "good heat map type setup".
    // User can adjust m_colorMinDb / m_colorMaxDb (via UI) to set the visual noise floor / dynamic range.
    // Dark blue = below min (noise floor), progressing through cyan/green/yellow to red = max (strong signals).
    auto dbToColor = [this](float db) -> QRgb {
        double range = m_colorMaxDb - m_colorMinDb;
        if (range <= 0.1) range = 100.0;
        float norm = std::clamp( static_cast<float>( (db - m_colorMinDb) / range ) , 0.0f, 1.0f);

        int r, g, b;
        if (norm < 0.2f) {
            // dark blue -> cyan (cold / noise floor)
            float t = norm / 0.2f;
            r = 10;
            g = static_cast<int>(80 + 140 * t);
            b = 180 + static_cast<int>(60 * t);
        } else if (norm < 0.45f) {
            // cyan -> green
            float t = (norm - 0.2f) / 0.25f;
            r = static_cast<int>(10 * (1-t));
            g = 220;
            b = static_cast<int>(240 - 200 * t);
        } else if (norm < 0.7f) {
            // green -> yellow
            float t = (norm - 0.45f) / 0.25f;
            r = static_cast<int>(255 * t);
            g = 220;
            b = static_cast<int>(40 * (1-t));
        } else {
            // yellow -> red (hot / strong signal)
            float t = (norm - 0.7f) / 0.3f;
            r = 255;
            g = static_cast<int>(220 - 180 * t);
            b = static_cast<int>(40 * (1-t));
        }
        return qRgb(std::clamp(r,0,255), std::clamp(g,0,255), std::clamp(b,0,255));
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
        float db = (src < bins) ? latestPower[src] : m_colorMinDb;
        m_waterfall.setPixel(x, 0, dbToColor(db));
    }

    m_waterfallPos = (m_waterfallPos + 1) % h;
}

double SpectrumWidget::freqFromX(int x) const
{
    double bw = (m_viewBandwidthHz > 0 ? m_viewBandwidthHz : m_sampleRate);
    double start = m_centerFreq - bw/2;
    int ww = width();
    if (ww <= 0) return m_centerFreq; // safe during early layout/paint
    return start + (x / double(ww)) * bw;
}

int SpectrumWidget::xFromFreq(double freq) const
{
    double bw = (m_viewBandwidthHz > 0 ? m_viewBandwidthHz : m_sampleRate);
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
    if (w <= 0 || h <= 0) return; // guard early paint during construction or 0-size layout
    int specH = h * 2 / 3;
    int wfH = h - specH;

    // background
    p.fillRect(rect(), QColor(20, 22, 25));

    // spectrum area
    p.setPen(QColor(60, 65, 70));
    p.drawRect(0, 0, w-1, specH-1);

    // grid + labels (use live members; labels are secondary and change infrequently)
    p.setPen(QColor(70, 75, 80));
    for (int i = 0; i <= 10; ++i) {
        int x = w * i / 10;
        p.drawLine(x, 0, x, specH);
        double f = freqFromX(x);
        p.setPen(QColor(140, 145, 150));
        p.drawText(x + 2, specH - 4, QString::number(f/1e6, 'f', 2) + "M");
        p.setPen(QColor(70, 75, 80));
    }

    // Snapshot the contended visual state under one *short* lock.
    // Release immediately. Then paint from the local copies.
    // This eliminates the previous second QMutexLocker on the non-recursive
    // m_dataMutex while the first was conceptually active, and prevents
    // holding the lock during QPainter work / drawImage / text.
    // Directly addresses the UI-thread paint deadlock (P0) that caused
    // "Responding: False" after entering the event loop.
    QVector<float> powerCopy;
    QImage wfCopy;
    std::vector<std::vector<float>> highResSnap; // recent high-bin rows for true-res zoomed waterfall
    double centerSnap = m_centerFreq;
    double srSnap = m_sampleRate;
    double viewBwSnap = (m_viewBandwidthHz > 0 ? m_viewBandwidthHz : m_sampleRate);
    double colorMinSnap = m_colorMinDb;
    double colorMaxSnap = m_colorMaxDb;

    {
        QMutexLocker lock(&m_dataMutex);
        powerCopy = m_powerDb;
        wfCopy = m_waterfall;
        centerSnap = m_centerFreq;
        srSnap = m_sampleRate;
        // Snapshot a useful number of recent high-res rows (newest at back). Paint will use these for zoomed detail.
        size_t take = std::min(m_highResHistory.size(), (size_t)96);
        auto it = m_highResHistory.end();
        for (size_t i = 0; i < take; ++i) {
            --it;
            highResSnap.push_back(*it); // reverse so [0] oldest in this local vec
        }
        std::reverse(highResSnap.begin(), highResSnap.end()); // now [0] oldest
    }

    // draw spectrum (from snapshot, lock already released)
    // When zoomed (narrow viewBw), we only use the power bins inside the view and map them across the full width.
    // This gives visibly higher resolution / finer detail on the top spectrum when zoomed in (matching the zoomed waterfall).
    if (!powerCopy.isEmpty()) {
        QPolygonF poly;
        int n = powerCopy.size();
        double fullStart = centerSnap - srSnap/2;
        double fullBw = srSnap;
        double viewStart = centerSnap - viewBwSnap / 2;
        double viewEnd = centerSnap + viewBwSnap / 2;

        for (int i = 0; i < n; ++i) {
            double binFreq = fullStart + (i + 0.5) * (fullBw / n);
            if (binFreq < viewStart || binFreq > viewEnd) continue;

            double rel = (binFreq - viewStart) / viewBwSnap;
            int x = static_cast<int>(rel * w);
            float db = powerCopy[i];
            // map using the (user adjustable via UI) color range so noise floor / sensitivity is reflected
            float range = static_cast<float>(colorMaxSnap - colorMinSnap);
            if (range < 1.0f) range = 100.0f;
            float norm = std::clamp( (db - static_cast<float>(colorMinSnap)) / range , 0.0f, 1.0f);
            int y = specH - 5 - static_cast<int>(norm * (specH - 10));
            poly << QPointF(x, y);
        }

        if (!poly.isEmpty()) {
            // filled
            QPolygonF fillPoly = poly;
            fillPoly << QPointF(w, specH) << QPointF(0, specH);
            p.setBrush(QColor(30, 120, 180, 60));
            p.setPen(Qt::NoPen);
            p.drawPolygon(fillPoly);

            // line
            p.setPen(QPen(QColor(100, 200, 255), 1.5));
            p.drawPolyline(poly);
        }
    } else {
        p.setPen(QColor(100, 180, 255));
        p.drawText(w/2 - 60, specH/2, "No spectrum data (waiting for IQ...)");
    }

    // Waterfall: render from high-res source history when we have it (true extra resolution on zoom).
    // This is the key fix for "waterfall zoom is visual crop/stretch, not true extra resolution".
    // We map each display column's frequency to the exact bin in the high-bin FFT rows and color from source power.
    // Falls back to the (possibly lower-res) image if no high-res history yet.
    QRect wfRect(0, specH, w, wfH);
    double fullBw = srSnap;
    double fullStart = centerSnap - fullBw / 2.0;
    double viewStart = centerSnap - viewBwSnap / 2.0;
    double viewEnd = centerSnap + viewBwSnap / 2.0;

    if (!highResSnap.empty()) {
        // Build a temp image for the wf area from the *source* high-res bins (correct zoomed detail).
        QImage wfSrc(w, wfH, QImage::Format_RGB32);
        wfSrc.fill(Qt::black);

        auto dbToColor = [&](float db) -> QRgb {
            double range = colorMaxSnap - colorMinSnap;
            if (range <= 0.1) range = 100.0;
            float norm = std::clamp(static_cast<float>((db - colorMinSnap) / range), 0.0f, 1.0f);
            int r, g, b;
            if (norm < 0.2f) { float t = norm / 0.2f; r = 10; g = static_cast<int>(80 + 140 * t); b = 180 + static_cast<int>(60 * t); }
            else if (norm < 0.45f) { float t = (norm - 0.2f) / 0.25f; r = static_cast<int>(10 * (1 - t)); g = 220; b = static_cast<int>(240 - 200 * t); }
            else if (norm < 0.7f) { float t = (norm - 0.45f) / 0.25f; r = static_cast<int>(255 * t); g = 220; b = static_cast<int>(40 * (1 - t)); }
            else { float t = (norm - 0.7f) / 0.3f; r = 255; g = static_cast<int>(220 - 180 * t); b = static_cast<int>(40 * (1 - t)); }
            return qRgb(std::clamp(r,0,255), std::clamp(g,0,255), std::clamp(b,0,255));
        };

        size_t rows = highResSnap.size();
        for (size_t hy = 0; hy < rows && hy < (size_t)wfH; ++hy) {
            const auto& row = highResSnap[hy]; // oldest first in snap
            int y = static_cast<int>(hy * (double)wfH / rows); // simple top=newest layout (matches scroll dir)
            if (row.empty()) continue;
            double binsF = (double)row.size();
            for (int x = 0; x < w; ++x) {
                double f = viewStart + (x / (double)w) * viewBwSnap;
                double binF = (f - fullStart) / (fullBw / binsF);
                int b = std::clamp((int)std::lround(binF), 0, (int)row.size() - 1);
                wfSrc.setPixel(x, y, dbToColor(row[b]));
            }
        }
        p.drawImage(wfRect, wfSrc);
    } else if (!wfCopy.isNull()) {
        // Legacy image path (full-view fast)
        double relLeft = (viewStart - fullStart) / fullBw;
        double relRight = (viewEnd - fullStart) / fullBw;
        int srcX = std::max(0, static_cast<int>(relLeft * wfCopy.width()));
        int srcW = std::max(1, static_cast<int>((relRight - relLeft) * wfCopy.width()));
        if (srcX + srcW > wfCopy.width()) srcW = wfCopy.width() - srcX;
        if (srcW > 0) p.drawImage(wfRect, wfCopy, QRect(srcX, 0, srcW, wfCopy.height()));
        else p.drawImage(wfRect, wfCopy);
    }
    p.setPen(QColor(80, 85, 90));
    p.drawRect(wfRect.adjusted(0, 0, -1, -1));

    // center marker (device center, top spectrum area)
    int cx = w / 2;
    p.setPen(QPen(QColor(255, 80, 80), 1, Qt::DashLine));
    p.drawLine(cx, 0, cx, specH);

    // last clicked tune position - full height line (works for clicks in spectrum OR waterfall area)
    if (m_tuneX >= 0 && m_tuneX < w) {
        p.setPen(QPen(QColor(255, 120, 120), 1, Qt::DashLine));
        p.drawLine(m_tuneX, 0, m_tuneX, h);
    }

    // title / info (use snapshot for consistency with the curve/waterfall of this frame)
    p.setPen(Qt::white);
    p.drawText(8, 16, QString("Center: %1 MHz   SR: %2 MS/s   (click anywhere incl. waterfall to tune monitor)")
                          .arg(centerSnap / 1e6, 0, 'f', 3)
                          .arg(srSnap / 1e6, 0, 'f', 2));
}

void SpectrumWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        double f = freqFromX(event->pos().x());
        emit frequencySelected(f);
        // Single onclick set freq (as requested). Do not enter continuous drag mode
        // that follows the mouse and "locks" it until release. This was causing the
        // mouse to attach and not unclick.
        // m_dragging = true;   // removed for onclick-only behavior
        m_lastMouseX = event->pos().x();
        m_tuneX = event->pos().x();
        update();
    }
}

void SpectrumWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging) {
        // Continuous drag tuning kept for backward compat but not entered on simple click now.
        double f = freqFromX(event->pos().x());
        emit frequencySelected(f);
        m_tuneX = event->pos().x();
        update();
    }
}

void SpectrumWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        // m_tuneX is kept so the dashed tune line remains visible after the click
        update();
    }
}

void SpectrumWidget::wheelEvent(QWheelEvent* event)
{
    // Zoom the display view (affects both spectrum curve and waterfall).
    // Smaller view BW = higher effective resolution / fine detail on both parts.
    double currentBw = (m_viewBandwidthHz > 0 ? m_viewBandwidthHz : m_sampleRate);
    double factor = (event->angleDelta().y() > 0) ? 0.7 : 1.4; // stronger zoom steps
    double newBw = std::clamp(currentBw * factor, 50e3, 20e6);
    m_viewBandwidthHz = newBw;
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

void SpectrumWidget::keyPressEvent(QKeyEvent* event)
{
    // Fine tuning and quick zoom from keyboard (focus on the spectrum widget).
    // Step size adapts to current view for "fine" when zoomed in.
    double step = 1000.0; // default 1 kHz
    double curView = (m_viewBandwidthHz > 0 ? m_viewBandwidthHz : m_sampleRate);
    if (curView > 0) step = std::max(50.0, curView / 400.0); // finer when zoomed

    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_A) {
        double f = m_centerFreq - step;
        emit frequencySelected(f);
        m_tuneX = xFromFreq(f);
        update();
    } else if (event->key() == Qt::Key_Right || event->key() == Qt::Key_D) {
        double f = m_centerFreq + step;
        emit frequencySelected(f);
        m_tuneX = xFromFreq(f);
        update();
    } else if (event->key() == Qt::Key_Up || event->key() == Qt::Key_W) {
        // zoom in (higher resolution view)
        m_viewBandwidthHz = std::max(20e3, curView * 0.7);
        update();
    } else if (event->key() == Qt::Key_Down || event->key() == Qt::Key_S) {
        // zoom out
        m_viewBandwidthHz = std::min(20e6, curView * 1.4);
        update();
    } else {
        QWidget::keyPressEvent(event);
    }
}

// Include generated moc file for Q_OBJECT (SpectrumWidget) when using AUTOMOC + header in include/
#include "moc_SpectrumWidget.cpp"  // kept for build compatibility; AUTOMOC enabled in CMake (P3 hygiene note: ideally remove manual include)