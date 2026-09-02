from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "src" / "SpectrumWidget.cpp"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"FAIL: {message}")


text = SRC.read_text(encoding="utf-8")

require("constexpr int kSpectrumAxisWidth = 48;" in text, "plot-axis width must be shared")
require("constexpr int kSpectrumRightMargin = 28;" in text, "right-margin width must be shared")

update = text.split("void SpectrumWidget::updateSpectrum", 1)[1].split(
    "void SpectrumWidget::updateIQ", 1
)[0]
require("const double previousSampleRate = m_sampleRate;" in update, "updateSpectrum must track prior sample rate")
require("const double previousViewBandwidth = m_viewBandwidthHz;" in update, "updateSpectrum must track prior view bandwidth")
require("const bool wasFullBandwidthView" in update, "updateSpectrum must detect full-band display mode")
require("m_viewBandwidthHz = sampleRateHz;" in update, "full-band view must follow the real sample rate")
require("std::clamp(previousViewBandwidth, 1000.0, sampleRateHz)" in update, "zoomed view must be clamped to capture bandwidth")

freq_from_x = text.split("double SpectrumWidget::freqFromX", 1)[1].split(
    "int SpectrumWidget::xFromFreq", 1
)[0]
require("const int plotLeft = kSpectrumAxisWidth;" in freq_from_x, "freqFromX must use plot-left, not widget-left")
require("ww - kSpectrumAxisWidth - kSpectrumRightMargin" in freq_from_x, "freqFromX must use plot width")
require("std::clamp(x, plotLeft, plotRight)" in freq_from_x, "freqFromX must clamp to the plot rectangle")

x_from_freq = text.split("int SpectrumWidget::xFromFreq", 1)[1].split(
    "// Map a power dB value", 1
)[0]
require("const int plotLeft = kSpectrumAxisWidth;" in x_from_freq, "xFromFreq must return plot-space coordinates")
require("ww - kSpectrumAxisWidth - kSpectrumRightMargin" in x_from_freq, "xFromFreq must use plot width")

paint = text.split("void SpectrumWidget::paintEvent", 1)[1].split(
    "void SpectrumWidget::mousePressEvent", 1
)[0]
require("centerSnap - viewBwSnap / 2.0" in paint, "frequency labels must use the frame snapshot")
require("static_cast<double>(i) / 10.0) * viewBwSnap" in paint, "grid labels must share the spectrum/waterfall span")
require("std::vector<std::vector<float>> highResSnap;" in paint, "paint must snapshot high-res waterfall history once")
require("const size_t take = std::min(m_highResHistory.size(), kMaxHighResHistory);" in paint, "waterfall must snapshot high-res history for full-band and zoomed views")
require("const bool zoomedView" not in paint, "waterfall history must not be limited to zoomed views")

set_view = text.split("void SpectrumWidget::setViewBandwidth", 1)[1].split(
    "void SpectrumWidget::setSquelchThreshold", 1
)[0]
require("std::clamp(bwHz, 1000.0, maxBw)" in set_view, "manual view bandwidth must be clamped to sample rate")

wheel = text.split("void SpectrumWidget::wheelEvent", 1)[1].split(
    "void SpectrumWidget::resizeEvent", 1
)[0]
require("std::clamp(currentBw * factor, 50e3, maxBw)" in wheel, "wheel zoom must be clamped to sample rate")

print("PASS: spectrum/waterfall axis alignment verified")
