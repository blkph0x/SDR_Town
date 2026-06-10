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

void SpectrumWidget::setSquelchThreshold(double db)
{
    m_squelchThresholdDb = db;
    update();
}

void SpectrumWidget::setLiveRms(double rmsDb)
{
    m_liveSignalDb = rmsDb;
    update();
}

void SpectrumWidget::setLiveLevels(double signalDb, double noiseFloorDb)
{
    m_liveSignalDb = signalDb;
    m_liveNoiseFloorDb = noiseFloorDb;
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

// Map a power dB value (in the current color range) to widget Y in the spectrum area (top 2/3).
// Higher dB (stronger) = higher on screen (smaller Y).
int SpectrumWidget::yFromDb(double db, int specH) const
{
    double range = m_colorMaxDb - m_colorMinDb;
    if (range < 1.0) range = 100.0;
    double norm = std::clamp( (db - m_colorMinDb) / range , 0.0, 1.0);
    return specH - 5 - static_cast<int>(norm * (specH - 10));
}

// Inverse: given a Y in the spectrum area, return the dB it corresponds to under current color range.
double SpectrumWidget::dbFromY(int y, int specH) const
{
    double range = m_colorMaxDb - m_colorMinDb;
    if (range < 1.0) range = 100.0;
    // y = specH-5 - norm*(specH-10)  =>  norm = (specH-5 - y) / (specH-10)
    double n = (specH - 5.0 - y) / (specH - 10.0);
    n = std::clamp(n, 0.0, 1.0);
    return m_colorMinDb + n * range;
}

// Dedicated fixed range for the interactive squelch line and live level marker.
// This is mapped over the spectrum curve height so the user can always drag from
// "always open" (bottom of curve area) to "force mute even loud signals" (top of curve area)
// completely independently of the WF Color Min/Max used for coloring.
static constexpr double kSquelchVizMinDb = -130.0;
static constexpr double kSquelchVizMaxDb =  40.0;

int SpectrumWidget::yFromSquelchViz(double db, int specH) const
{
    double range = kSquelchVizMaxDb - kSquelchVizMinDb;
    double norm = std::clamp( (db - kSquelchVizMinDb) / range , 0.0, 1.0);
    return specH - 5 - static_cast<int>(norm * (specH - 10));
}

double SpectrumWidget::squelchVizDbFromY(int y, int specH) const
{
    double range = kSquelchVizMaxDb - kSquelchVizMinDb;
    double n = (specH - 5.0 - y) / (specH - 10.0);
    n = std::clamp(n, 0.0, 1.0);
    return kSquelchVizMinDb + n * range;   // high on screen (low y) = high (less negative / positive) dB
}

void SpectrumWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    int w = width();
    int h = height();
    if (w <= 0 || h <= 0) return; // guard early paint during construction or 0-size layout

    // Reserve left margin for dynamic dB scale (power axis). This makes the "dynamic db numbers on the side"
    // the user asked for, using the live color range so it stays in sync with what the user sees in the WF colors.
    const int axisW = 48;   // left dB axis gutter
    const int rightMargin = 28; // right side for squelch grab bar / handle (easy to grab)

    int specH = h * 2 / 3;
    int wfH = h - specH;

    // background
    p.fillRect(rect(), QColor(20, 22, 25));

    // spectrum area rect (shifted right for the axis)
    QRect specRect(axisW, 0, w - axisW - rightMargin, specH);
    QRect wfRect(axisW, specH, w - axisW - rightMargin, wfH);

    // Draw left dB axis (dynamic, based on current colorMin/Max)
    {
        p.setPen(QColor(90, 95, 100));
        p.drawLine(axisW-1, 0, axisW-1, h); // separator line

        double cmin = m_colorMinDb;
        double cmax = m_colorMaxDb;
        double range = cmax - cmin;
        if (range < 1.0) range = 100.0;

        // Nice step: 10 dB major, 5 dB minor within the current visible range
        int step = 10;
        if (range > 80) step = 20;
        else if (range < 30) step = 5;

        p.setPen(QColor(140, 145, 150));
        QFont smallFont = p.font(); smallFont.setPointSize(8); p.setFont(smallFont);

        for (double db = std::floor(cmin / step) * step; db <= cmax + 0.1; db += step) {
            int y = (db >= cmin && db <= cmax)
                ? yFromDb(db, specH)   // use the spectrum-area mapping for the top part
                : -1;

            // Also draw ticks extending into the waterfall area at the equivalent relative position
            // (simple linear extension of the same norm for the whole height is acceptable for a threshold viz)
            double norm = std::clamp( (db - cmin) / range , 0.0, 1.0);
            int yFull = static_cast<int>( norm * (h - 8) );

            // tick on axis
            p.drawLine(axisW - 8, yFull, axisW - 2, yFull);

            // label (only major ones or when space allows)
            QString lbl = QString::number(db, 'f', 0);
            p.drawText(2, yFull + 3, lbl);
        }
        p.setFont(QFont()); // restore
    }

    // spectrum area border (inside the shifted rect)
    p.setPen(QColor(60, 65, 70));
    p.drawRect(specRect.adjusted(0,0,-1,-1));

    // grid + labels (use live members; labels are secondary and change infrequently)
    // Grid now drawn inside the plot area (after left dB axis).
    p.setPen(QColor(70, 75, 80));
    int plotW = specRect.width();
    for (int i = 0; i <= 10; ++i) {
        int x = specRect.left() + plotW * i / 10;
        p.drawLine(x, specRect.top(), x, specRect.bottom());
        double f = freqFromX( (x - specRect.left()) * width() / std::max(1, plotW) );  // approx using original logic scaled
        // Simpler: recompute using the view
        p.setPen(QColor(140, 145, 150));
        p.drawText(x + 2, specRect.bottom() - 4, QString::number(f/1e6, 'f', 2) + "M");
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
    double squelchSnap = m_squelchThresholdDb;
    double liveSignalSnap = m_liveSignalDb;
    double liveNoiseSnap = m_liveNoiseFloorDb;

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
        int plotLeft = specRect.left();
        int plotWidth = specRect.width();

        for (int i = 0; i < n; ++i) {
            double binFreq = fullStart + (i + 0.5) * (fullBw / n);
            if (binFreq < viewStart || binFreq > viewEnd) continue;

            double rel = (binFreq - viewStart) / viewBwSnap;
            int x = plotLeft + static_cast<int>(rel * plotWidth);
            float db = powerCopy[i];
            // map using the (user adjustable via UI) color range so noise floor / sensitivity is reflected
            float range = static_cast<float>(colorMaxSnap - colorMinSnap);
            if (range < 1.0f) range = 100.0f;
            float norm = std::clamp( (db - static_cast<float>(colorMinSnap)) / range , 0.0f, 1.0f);
            int y = yFromDb(db, specH);   // now consistent with the dB axis we just drew
            // clamp y into the specRect vertically
            y = std::clamp(y, specRect.top() + 2, specRect.bottom() - 2);
            poly << QPointF(x, y);
        }

        if (!poly.isEmpty()) {
            // filled (bottom at the spec area bottom)
            QPolygonF fillPoly = poly;
            fillPoly << QPointF(plotLeft + plotWidth, specRect.bottom()) << QPointF(plotLeft, specRect.bottom());
            p.setBrush(QColor(30, 120, 180, 60));
            p.setPen(Qt::NoPen);
            p.drawPolygon(fillPoly);

            // line
            p.setPen(QPen(QColor(100, 200, 255), 1.5));
            p.drawPolyline(poly);
        }
    } else {
        p.setPen(QColor(100, 180, 255));
        p.drawText(specRect.center().x() - 80, specRect.center().y(), "No spectrum data (waiting for IQ...)");
    }

    // Waterfall: render from high-res source history when we have it (true extra resolution on zoom).
    // This is the key fix for "waterfall zoom is visual crop/stretch, not true extra resolution".
    // We map each display column's frequency to the exact bin in the high-bin FFT rows and color from source power.
    // Falls back to the (possibly lower-res) image if no high-res history yet.
    // wfRect and specRect already computed with left dB axis + right margin reserved.
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
        for (int y = 0; y < wfH; ++y) {
            size_t ageFromNewest = std::min(rows - 1, (size_t)((double)y * rows / std::max(1, wfH)));
            size_t rowIndex = rows - 1 - ageFromNewest; // newest at top, oldest at bottom
            const auto& row = highResSnap[rowIndex];
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

    // === Interactive squelch threshold line + right grab bar ===
    // The SQ threshold, signal marker, and noise-floor marker all use the same dB scale
    // as the spectrum axis. This keeps the visual line aligned with the actual gate metric.
    {
        int plotLeft = specRect.left();
        int plotRight = specRect.right();

        int sqY = yFromDb(squelchSnap, specH);
        sqY = std::clamp(sqY, specRect.top() + 1, specRect.bottom() - 1);

        // Horizontal "cut" line (dashed orange) across the plot area + into waterfall for visibility
        p.setPen(QPen(QColor(255, 180, 60), 1.8, Qt::DashLine));
        p.drawLine(plotLeft + 2, sqY, plotRight - 2, sqY);
        // faint extension into waterfall
        p.setPen(QPen(QColor(255, 180, 60, 90), 1, Qt::DashLine));
        p.drawLine(plotLeft + 2, sqY, plotRight - 2, h - 3);

        // Label for the SQ line (always visible)
        p.setPen(QColor(255, 210, 90));
        p.drawText(plotLeft + 6, std::max(specRect.top() + 12, sqY - 2), QString("SQ %1 dB").arg(squelchSnap, 0, 'f', 0));

        if (std::isfinite(liveNoiseSnap)) {
            int nfY = yFromDb(liveNoiseSnap, specH);
            nfY = std::clamp(nfY, specRect.top() + 1, specRect.bottom() - 1);
            p.setPen(QPen(QColor(80, 220, 120), 1.5));  // green = local RF noise floor
            p.drawLine(plotLeft + 2, nfY, plotRight - 2, nfY);
            p.setPen(QColor(100, 230, 140));
            p.drawText(plotRight - 78, std::min(specRect.bottom() - 4, nfY + 10), QString("NF %1").arg(liveNoiseSnap, 0, 'f', 1));
        }

        if (std::isfinite(liveSignalSnap)) {
            int sigY = yFromDb(liveSignalSnap, specH);
            sigY = std::clamp(sigY, specRect.top() + 1, specRect.bottom() - 1);
            p.setPen(QPen(QColor(220, 235, 255), 1.2));
            p.drawLine(plotLeft + 2, sigY, plotRight - 2, sigY);
            p.setPen(QColor(220, 235, 255));
            p.drawText(plotRight - 82, std::max(specRect.top() + 12, sigY - 3), QString("SIG %1").arg(liveSignalSnap, 0, 'f', 1));
        }

        // === Right side grab bar + handle (easy target for mouse) ===
        int rightBarX = w - rightMargin + 4;
        int handleH = 13;
        int handleY = sqY - handleH / 2;
        QRect handleRect(w - rightMargin + 1, handleY, rightMargin - 3, handleH);

        // vertical "ruler" on the far right
        p.setPen(QPen(QColor(255, 180, 60), 3));
        p.drawLine(rightBarX, specRect.top() + 2, rightBarX, specRect.bottom() - 2);

        // the handle itself
        p.setBrush(QColor(255, 185, 70, 200));
        p.setPen(QPen(QColor(255, 230, 140), 1));
        p.drawRoundedRect(handleRect, 4, 4);

        // arrows for "draggable vertically"
        p.setPen(QColor(50, 35, 10));
        int cxh = handleRect.center().x();
        p.drawLine(cxh-4, handleY + 4, cxh, handleY + 2);
        p.drawLine(cxh+1, handleY + 2, cxh+5, handleY + 4);
        p.drawLine(cxh-4, handleY + handleH - 4, cxh, handleY + handleH - 2);
        p.drawLine(cxh+1, handleY + handleH - 2, cxh+5, handleY + handleH - 4);
    }

    // center marker (device center, top spectrum area)
    int cx = specRect.left() + specRect.width() / 2;
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
        int mx = event->pos().x();
        int my = event->pos().y();
        int ww = width();
        int hh = height();
        int specHlocal = hh * 2 / 3;

        // Priority: right-side squelch grab bar or close to the horizontal squelch line → interactive squelch drag
        bool nearRightBar = (mx >= ww - 35);
        int currentSqY = yFromDb(m_squelchThresholdDb, specHlocal);
        bool nearLine = std::abs(my - currentSqY) <= 10;

        if (nearRightBar || nearLine) {
            m_squelchDragging = true;
            double db = dbFromY(my, specHlocal);
            db = std::clamp(db, -130.0, 40.0);
            m_squelchThresholdDb = db;
            emit squelchThresholdChanged(db);
            update();
            return;  // consume as squelch adjust, do not tune
        }

        // Normal behavior: click anywhere else (spectrum or waterfall) = tune to that freq
        double f = freqFromX(mx);
        emit frequencySelected(f);
        m_lastMouseX = mx;
        m_tuneX = mx;
        update();
    }
}

void SpectrumWidget::mouseMoveEvent(QMouseEvent* event)
{
    int mx = event->pos().x();
    int my = event->pos().y();
    int ww = width();
    int hh = height();
    int specHlocal = hh * 2 / 3;

    // Hover feedback: change cursor when over the right squelch bar or the line (affordance)
    int currentSqY = yFromDb(m_squelchThresholdDb, specHlocal);
    bool overSquelchZone = (mx >= ww - 35) || (std::abs(my - currentSqY) <= 10);
    if (overSquelchZone) {
        setCursor(Qt::SplitVCursor);
    } else if (cursor().shape() != Qt::ArrowCursor) {
        unsetCursor();
    }

    if (m_squelchDragging) {
        double db = dbFromY(my, specHlocal);
        db = std::clamp(db, -130.0, 40.0);
        if (std::abs(db - m_squelchThresholdDb) > 0.05) {
            m_squelchThresholdDb = db;
            emit squelchThresholdChanged(db);
            update();
        }
        return;
    }

    if (m_dragging) {
        // Continuous drag tuning kept for backward compat but not entered on simple click now.
        double f = freqFromX(mx);
        emit frequencySelected(f);
        m_tuneX = mx;
        update();
    }
}

void SpectrumWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        m_squelchDragging = false;
        unsetCursor();
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
