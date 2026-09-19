#include "WorkspaceLayout.h"
#include "BandPlan.h"
#include "BandPlanDialog.h"
#include "SpectrumWidget.h"
#include "RdsStatusWidget.h"
#include <QMouseEvent>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTableWidget>
#include <QDockWidget>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QSettings>
#include <QTemporaryDir>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QTemporaryDir settingsRoot;
    if (!settingsRoot.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsRoot.path());
    return Catch::Session().run(argc, argv);
}

TEST_CASE("Waterfall drag previews without repeated hardware tune requests", "[waterfall][gui]") {
    SpectrumWidget widget;
    widget.resize(1000,300);
    widget.setCenterFreq(100e6);
    widget.setSampleRate(2e6);
    int tunes=0, squelches=0; double selected=0;
    QObject::connect(&widget,&SpectrumWidget::frequencySelected,[&](double f){++tunes;selected=f;});
    QObject::connect(&widget,&SpectrumWidget::squelchThresholdChanged,[&](double){++squelches;});
    auto mouse=[&](QEvent::Type type,int x,int y,Qt::MouseButton button,Qt::MouseButtons buttons) {
        QMouseEvent event(type,QPointF(x,y),QPointF(x,y),button,buttons,Qt::NoModifier);
        QApplication::sendEvent(&widget,&event);
    };
    mouse(QEvent::MouseButtonPress,300,270,Qt::LeftButton,Qt::LeftButton);
    mouse(QEvent::MouseMove,600,270,Qt::NoButton,Qt::LeftButton);
    REQUIRE(tunes==0);
    widget.setCenterFreq(110e6); // A live display refresh must not move the gesture axis.
    mouse(QEvent::MouseButtonRelease,700,270,Qt::LeftButton,Qt::NoButton);
    REQUIRE(tunes==1);
    REQUIRE(std::abs(selected-(99e6+(700-48)*2e6/924))<0.01);
    REQUIRE(squelches==0);
    mouse(QEvent::MouseButtonPress,980,80,Qt::LeftButton,Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease,980,80,Qt::LeftButton,Qt::NoButton);
    REQUIRE(tunes==1);
    REQUIRE(squelches==1);
    mouse(QEvent::MouseButtonPress,500,270,Qt::LeftButton,Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease,500,270,Qt::LeftButton,Qt::NoButton);
    REQUIRE(tunes==2);
}

TEST_CASE("RDS presentation hides stale and unrelated station metadata", "[rds][gui]") {
    RdsStatusWidget widget;
    RdsMpxSnapshot snapshot;
    snapshot.status="Receiving RDS"; snapshot.targetHz=98.1e6; snapshot.lastGroupMs=10000;
    snapshot.station.identified=true; snapshot.station.pi=0x6201;
    snapshot.station.programmeService="TEST FM"; snapshot.station.radioText="<b>Plain text</b>";
    widget.present(snapshot,true,98.1e6,11000);
    REQUIRE(widget.text().contains("TEST FM"));
    REQUIRE(widget.textFormat()==Qt::PlainText);
    widget.present(snapshot,true,99.1e6,11000);
    REQUIRE_FALSE(widget.text().contains("TEST FM"));
    widget.present(snapshot,true,98.1e6,16000);
    REQUIRE(widget.text()=="RDS: No recent data");
    widget.present(snapshot,false,98.1e6,11000);
    REQUIRE(widget.text()=="RDS: WFM inactive");
}

TEST_CASE("NFM tone display is isolated from stale or retuned receiver evidence", "[ctcss][gui]") {
    RdsStatusWidget widget; CtcssSnapshot tone;
    tone.frequencyHz=123; tone.targetHz=476.4625e6; tone.updatedMs=1000;
    widget.presentTone(tone,true,tone.targetHz,1500);
    REQUIRE(widget.text()=="CTCSS: 123.0 Hz");
    widget.presentTone(tone,true,477e6,1500);
    REQUIRE_FALSE(widget.text().contains("123.0"));
    widget.presentTone(tone,true,tone.targetHz,4000);
    REQUIRE_FALSE(widget.text().contains("123.0"));
    widget.presentTone(tone,false,tone.targetHz,1500);
    REQUIRE(widget.text()=="NFM tones: inactive");
}

TEST_CASE("Waterfall band sections clip and resolve overlapping service priorities", "[bandplan][gui]") {
    BandPlanProfile profile;
    BandPlanEntry broad; broad.name="Broad"; broad.startHz=80e6; broad.endHz=110e6; broad.mode=DemodMode::WFM;
    BandPlanEntry narrow=broad; narrow.name="Local"; narrow.startHz=99e6; narrow.endHz=100e6; narrow.priority=1;
    profile.entries={broad,narrow};
    const auto sections=visibleBandSections(profile,98e6,101e6);
    REQUIRE(sections.size()==3);
    REQUIRE(sections[0].startHz==98e6);
    REQUIRE(sections[0].endHz==99e6);
    REQUIRE(sections[1].name=="Local");
    REQUIRE(sections[2].startHz==100e6);
    REQUIRE(sections[2].endHz==101e6);
    REQUIRE(visibleBandSections(profile,120e6,121e6).empty());
    REQUIRE(visibleBandSections(profile,101e6,98e6).empty());
}

TEST_CASE("Band-plan dialog previews without applying and persists explicit Apply", "[bandplan][gui]") {
    auto& catalog = BandPlanCatalog::instance();
    REQUIRE(catalog.select("AU"));
    int applied = 0;
    BandPlanDialog dialog([&] { ++applied; });
    auto* region = dialog.findChild<QComboBox*>("bandplan.region");
    auto* country = dialog.findChild<QComboBox*>("bandplan.country");
    auto* location = dialog.findChild<QComboBox*>("bandplan.location");
    auto* table = dialog.findChild<QTableWidget*>();
    REQUIRE(region); REQUIRE(country); REQUIRE(location); REQUIRE(table);
    region->setCurrentText("ITU Region 2");
    REQUIRE(country->currentText() == "United States");
    REQUIRE(location->currentData().toString() == "US");
    REQUIRE(table->rowCount() > 0);
    REQUIRE(catalog.active()->id == "AU");
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    REQUIRE(buttons);
    buttons->button(QDialogButtonBox::Apply)->click();
    REQUIRE(applied == 1);
    REQUIRE(catalog.active()->id == "US");
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "SDR_Town", "SDR Town");
    REQUIRE(settings.fileName().endsWith(".ini"));
    REQUIRE(settings.value("bandplan/selected").toString() == "US");
    region->setCurrentText("ITU Region 1");
    dialog.reject();
    REQUIRE(catalog.active()->id == "US");
    catalog.select("AU");
}

TEST_CASE("Workspace presets preserve widgets and edited values", "[workspace]") {
    QMainWindow window;
    WorkspaceLayout layout(&window);
    auto* field = new QLineEdit("420.35000");
    auto* saved = layout.addPanel("saved", "Saved", field);
    auto* p25 = layout.addPanel("p25", "P25", new QWidget);
    auto* capture = layout.addPanel("capture", "Capture", new QWidget);
    auto* tx = layout.addPanel("tx", "TX", new QWidget);
    window.show();
    for (const auto& preset : {"listening", "trunking", "hf", "analysis"}) {
        REQUIRE(layout.applyPreset(preset));
        QApplication::processEvents();
        REQUIRE(field->text() == "420.35000");
        REQUIRE(saved->findChild<QLineEdit*>() == field);
        REQUIRE(tx->isHidden());
        REQUIRE(window.tabifiedDockWidgets(saved).contains(capture));
        if (QString(preset) == "hf" || QString(preset) == "listening") REQUIRE(p25->isHidden());
    }
    REQUIRE_FALSE(layout.applyPreset("unknown"));
    REQUIRE(layout.preset() == "analysis");
    p25->close();
    REQUIRE(window.findChild<QDockWidget*>("workspace.p25") == p25);
    REQUIRE(layout.applyPreset("trunking"));
    REQUIRE_FALSE(p25->isHidden());
}

TEST_CASE("Workspace roundtrips geometry visibility and lock state", "[workspace]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    QSettings settings(directory.filePath("layout.ini"), QSettings::IniFormat);
    {
        QMainWindow window;
        WorkspaceLayout layout(&window);
        layout.addPanel("saved", "Saved", new QWidget);
        auto* p25 = layout.addPanel("p25", "P25", new QWidget);
        layout.applyPreset("trunking");
        window.resize(1100, 750);
        window.show();
        QApplication::processEvents();
        p25->hide();
        layout.setLocked(true);
        layout.save(settings);
    }
    QMainWindow restored;
    WorkspaceLayout layout(&restored);
    layout.addPanel("saved", "Saved", new QWidget);
    auto* p25 = layout.addPanel("p25", "P25", new QWidget);
    REQUIRE(layout.restore(settings));
    REQUIRE(layout.preset() == "trunking");
    REQUIRE(layout.isLocked());
    REQUIRE(p25->isHidden());
    REQUIRE_FALSE(p25->features().testFlag(QDockWidget::DockWidgetMovable));
    layout.setLocked(false);
    REQUIRE(p25->features().testFlag(QDockWidget::DockWidgetFloatable));
    settings.setValue("workspace/v1/state", QByteArray("corrupt"));
    REQUIRE_FALSE(layout.restore(settings));
    REQUIRE(layout.preset() == "listening");
    REQUIRE_FALSE(layout.isLocked());
}

TEST_CASE("DCS status displays preferred label and hides stale data", "[workspace]") {
    RdsStatusWidget widget;
    CtcssSnapshot tone; tone.targetHz=100e6; tone.updatedMs=1000;
    DcsSnapshot dcs; dcs.targetHz=100e6; dcs.updatedMs=1000;
    dcs.identities={{0023,false},{0047,true}};
    widget.presentTone(tone,true,100e6,1100,dcs);
    REQUIRE(widget.text().contains("DCS: 023N"));
    REQUIRE_FALSE(widget.text().contains("equivalent"));
    REQUIRE_FALSE(widget.text().contains("047I"));
    dcs.updatedMs=1; tone.updatedMs=3000;
    widget.presentTone(tone,true,100e6,3000,dcs);
    REQUIRE_FALSE(widget.text().contains("023N"));
    widget.presentTone(tone,false,100e6,3000,dcs);
    REQUIRE(widget.text().contains("inactive"));
}

TEST_CASE("Workspace menu actions and compact layouts remain usable", "[workspace]") {
    QMainWindow window;
    WorkspaceLayout layout(&window);
    window.setCentralWidget(new QWidget);
    auto* dock = layout.addPanel("saved", "Saved", new QLineEdit("editable"));
    QMenu menu;
    layout.populateMenu(&menu);
    window.resize(800, 600);
    window.show();
    layout.applyPreset("listening");
    QApplication::processEvents();
    REQUIRE(window.width() == 800);
    REQUIRE(window.height() == 600);
    dock->setFloating(true);
    REQUIRE(dock->isFloating());
    QAction* reset = nullptr;
    for (auto* action : menu.actions()) if (action->text() == "Reset Layout") reset = action;
    REQUIRE(reset);
    layout.setLocked(true);
    reset->trigger();
    REQUIRE_FALSE(dock->isFloating());
    REQUIRE_FALSE(layout.isLocked());
    REQUIRE_FALSE(window.grab().isNull());
}
