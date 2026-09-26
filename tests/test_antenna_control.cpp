#include "AntennaControl.h"
#include "AntennaControlWindow.h"
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QElapsedTimer>
#include <QSettings>
#include <QStandardPaths>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QMessageBox>
#include <QAbstractButton>
#include <QPlainTextEdit>
#include <QDir>
#include <limits>

int main(int argc,char** argv){QApplication app(argc,argv);QStandardPaths::setTestModeEnabled(true);
    app.setOrganizationName("SDR_Town_Tests");app.setApplicationName("antenna-fixture");QSettings().clear();return Catch::Session().run(argc,argv);}
namespace{
bool waitFor(const std::function<bool()>& ready,int ms=2500){QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<ms){QApplication::processEvents();QThread::msleep(1);}return ready();}
class FakeController{
public:
    QTcpServer server;QStringList commands;bool stall=false;bool split=false;bool malformed=false;int ptt=1;int swrCode=0;
    FakeController(){REQUIRE(server.listen(QHostAddress::LocalHost));QObject::connect(&server,&QTcpServer::newConnection,&server,[this]{
        while(auto* s=server.nextPendingConnection())QObject::connect(s,&QTcpSocket::readyRead,s,[this,s]{
            while(s->canReadLine()){
                const auto cmd=s->readLine();commands<<QString::fromLatin1(cmd).trimmed();if(stall)continue;
                QByteArray reply;
                if(cmd=="+p\n")reply="get_pos:\nAzimuth: 180\nElevation: 45\nRPRT 0\n";
                else if(cmd.startsWith("+P "))reply="set_pos: "+cmd.mid(3).trimmed()+"\nRPRT 0\n";
                else if(cmd=="+S\n")reply="stop:\nRPRT 0\n";
                else if(cmd=="+t\n")reply="get_ptt:\nPTT: "+QByteArray::number(ptt)+"\nRPRT 0\n";
                else if(cmd=="+l SWR\n")reply="get_level: SWR\nLevel Value: 1.50\nRPRT "+QByteArray::number(swrCode)+"\n";
                else FAIL("Unexpected command");
                if(malformed)reply="wrong_command:\nRPRT 0\n";
                if(split){const int half=reply.size()/2;s->write(reply.left(half));QTimer::singleShot(20,s,[s,tail=reply.mid(half)]{s->write(tail);});}
                else s->write(reply);
            }
        });
    });}
};
}
TEST_CASE("Rotor finite limits fail closed"){
    RotorLimits l;CHECK(l.valid());CHECK(l.contains(360,90));CHECK_FALSE(l.contains(-1,45));CHECK_FALSE(l.contains(180,91));
    CHECK_FALSE(l.contains(std::numeric_limits<double>::quiet_NaN(),0));l.minAz=400;CHECK_FALSE(l.valid());
}
TEST_CASE("Rotor connect never moves and armed movement respects limits"){
    FakeController fake;fake.split=true;RotatorController rotor;
    rotor.connectTo("127.0.0.1",fake.server.serverPort(),{});REQUIRE(waitFor([&]{return rotor.fresh();}));
    CHECK(fake.commands==QStringList{"+p"});CHECK_FALSE(rotor.moveTo(180,45));REQUIRE(rotor.arm());
    CHECK_FALSE(rotor.moveTo(361,45));REQUIRE(rotor.moveTo(200,50));
    REQUIRE(waitFor([&]{return fake.commands.contains("+P 200.000 50.000");}));
    rotor.stop();REQUIRE(waitFor([&]{return fake.commands.contains("+S");}));CHECK_FALSE(rotor.armed());
    rotor.disconnectFromController();REQUIRE(waitFor([&]{return !rotor.connected();}));
}
TEST_CASE("Rotor timeout and mismatched responses cannot arm"){
    FakeController fake;fake.malformed=true;RotatorController rotor;
    rotor.connectTo("127.0.0.1",fake.server.serverPort(),{});REQUIRE(waitFor([&]{return !fake.commands.empty()&&!rotor.connected();}));CHECK_FALSE(rotor.arm());
    fake.malformed=false;fake.stall=true;rotor.connectTo("127.0.0.1",fake.server.serverPort(),{});
    REQUIRE(waitFor([&]{return rotor.connected();}));REQUIRE(waitFor([&]{return !rotor.connected();}));CHECK_FALSE(rotor.fresh());CHECK_FALSE(rotor.arm());
}
TEST_CASE("SWR is read-only and absent without hardware transmission"){
    FakeController fake;fake.ptt=0;SwrMonitor monitor;int readings=0;double value=0;
    QObject::connect(&monitor,&SwrMonitor::reading,[&](double v){++readings;value=v;});
    monitor.connectTo("127.0.0.1",fake.server.serverPort());REQUIRE(waitFor([&]{return fake.commands.contains("+t");}));
    CHECK(readings==0);CHECK_FALSE(fake.commands.contains("+l SWR"));fake.ptt=1;
    REQUIRE(waitFor([&]{return readings>0;}));CHECK(value==1.5);
    for(const auto& cmd:fake.commands)CHECK((cmd=="+t"||cmd=="+l SWR"));
    monitor.disconnectMeter();
}
TEST_CASE("Unsupported SWR never produces a numeric reading"){
    FakeController fake;fake.swrCode=-4;SwrMonitor monitor;int readings=0;bool unavailable=false;
    QObject::connect(&monitor,&SwrMonitor::reading,[&](double){++readings;});
    QObject::connect(&monitor,&SwrMonitor::status,[&](const QString& text){if(text.contains("Hamlib -4"))unavailable=true;});
    monitor.connectTo("127.0.0.1",fake.server.serverPort());REQUIRE(waitFor([&]{return unavailable;}));CHECK(readings==0);
}
TEST_CASE("Stop waits only for in-flight query and never resends movement"){
    FakeController fake;RotatorController rotor;
    rotor.connectTo("127.0.0.1",fake.server.serverPort(),{});REQUIRE(waitFor([&]{return rotor.fresh();}));REQUIRE(rotor.arm());
    REQUIRE(rotor.moveTo(190,40));rotor.stop();CHECK_FALSE(rotor.armed());
    REQUIRE(waitFor([&]{return fake.commands.contains("+S");}));
    REQUIRE(fake.commands.size()>=3);CHECK(fake.commands[1]=="+P 190.000 40.000");CHECK(fake.commands[2]=="+S");
}
TEST_CASE("Antenna window initial state is disarmed and responsive"){
    AntennaControlWindow window;window.show();QApplication::processEvents();
    auto* move=window.findChild<QPushButton*>("rotorMove");REQUIRE(move);CHECK_FALSE(move->isEnabled());
    REQUIRE(window.findChild<QLabel*>("swrReading"));
    if(!qEnvironmentVariable("SDR_TOWN_ANTENNA_SCREENSHOT").isEmpty())
        REQUIRE(window.grab().save(qEnvironmentVariable("SDR_TOWN_ANTENNA_SCREENSHOT")));
    window.close();
}
TEST_CASE("Antenna GUI connect arm move and close follow the real controller path"){
    FakeController fake;AntennaControlWindow window;window.show();
    auto* port=window.findChild<QSpinBox*>("rotorPort");REQUIRE(port);port->setValue(fake.server.serverPort());
    QPushButton* connectButton=nullptr;
    for(auto* button:window.findChildren<QPushButton*>())if(button->text()=="Connect")connectButton=button;
    REQUIRE(connectButton);connectButton->click();
    auto* position=window.findChild<QLabel*>("rotorPosition");REQUIRE(position);
    const bool positioned=waitFor([&]{return position->text().contains("180.0");});
    INFO(fake.commands.join(" | ").toStdString());
    INFO(window.findChild<QPlainTextEdit*>()->toPlainText().toStdString());
    REQUIRE(positioned);
    auto* arm=window.findChild<QCheckBox*>("rotorArm");REQUIRE(arm);
    QTimer::singleShot(0,[]{if(auto* dialog=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))dialog->button(QMessageBox::Yes)->click();});
    arm->click();REQUIRE(arm->isChecked());
    window.findChild<QDoubleSpinBox*>("targetAz")->setValue(210);
    window.findChild<QDoubleSpinBox*>("targetEl")->setValue(30);
    auto* move=window.findChild<QPushButton*>("rotorMove");REQUIRE(waitFor([&]{return move->isEnabled();}));move->click();
    REQUIRE(waitFor([&]{return fake.commands.contains("+P 210.000 30.000");}));
    window.close();REQUIRE(waitFor([&]{return fake.commands.contains("+S");}));CHECK_FALSE(arm->isChecked());
}
