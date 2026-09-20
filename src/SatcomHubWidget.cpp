#include "SatcomHubWidget.h"
#include "SatcomScannerWidget.h"
#include "SatcomScannerEngine.h"
#include "InmarsatWidget.h"
#include "InmarsatEngine.h"
#include "AircraftMapWidget.h"

#include <QHideEvent>
#include <QShowEvent>
#include <QTabWidget>
#include <QVBoxLayout>

SatcomHubWidget::SatcomHubWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    tabs_ = new QTabWidget(this);
    tabs_->setDocumentMode(true);
    root->addWidget(tabs_);
    // Tabs are created on first show — constructing Satcom/Inmarsat/Aircraft at
    // MainWindow startup was enough to steal the GUI/DSP path during normal WFM.
}

void SatcomHubWidget::ensureTabs() {
    if (satcom_) return;

    satcom_ = new SatcomScannerWidget(tabs_);
    satcom_->setWindowFlags(Qt::Widget);
    inmarsat_ = new InmarsatWidget(tabs_);
    inmarsat_->setWindowFlags(Qt::Widget);
    aircraft_ = new AircraftMapWidget(tabs_);
    aircraft_->setEmbedded(true);

    tabs_->addTab(satcom_, "Satcom");
    tabs_->addTab(inmarsat_, "Inmarsat");
    tabs_->addTab(aircraft_, "Aircraft");

    connect(satcom_, &SatcomScannerWidget::requestOpenSstvLive, this, &SatcomHubWidget::requestOpenSstvLive);
    // Disarm only when the panel is first opened — never construct the scanner
    // engine during normal WFM listening startup.
    SatcomScannerEngine::instance().disarmPass();
}

void SatcomHubWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    ensureTabs();
}

void SatcomHubWidget::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    SatcomScannerEngine::instance().stopRecording();
    SatcomScannerEngine::instance().stop();
    SatcomScannerEngine::instance().disarmPass();
    InmarsatEngine::instance().stop();
}

void SatcomHubWidget::showSatcomTab() {
    ensureTabs();
    tabs_->setCurrentWidget(satcom_);
}

void SatcomHubWidget::showInmarsatTab() {
    ensureTabs();
    tabs_->setCurrentWidget(inmarsat_);
}

void SatcomHubWidget::showAircraftTab() {
    ensureTabs();
    tabs_->setCurrentWidget(aircraft_);
}
