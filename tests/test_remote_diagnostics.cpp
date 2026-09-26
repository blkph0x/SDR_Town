#include "RemoteDiagnostics.h"
#include "DiagnosticsMenu.h"
#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QAction>
#include <QTimer>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QUuid>
#include <QSettings>
#include <vector>

int main(int argc,char** argv) {
    QApplication app(argc,argv);QStandardPaths::setTestModeEnabled(true);
    app.setOrganizationName("SDR_Town_Tests");
    app.setApplicationName("diagnostics-"+QUuid::createUuid().toString(QUuid::WithoutBraces));
    // No real endpoint/network is used by these tests. Environment overrides
    // are deliberately removed only in this disposable test process.
    for(const char* k:{"SDR_TOWN_DIAG_CONFIG","SDR_TOWN_DIAG_URL","SDR_TOWN_DIAG_TOKEN"})qunsetenv(k);
    return Catch::Session().run(argc,argv);
}
namespace {
RemoteDiagnosticsConfig config(std::initializer_list<QByteArray> input) {
    std::vector<QByteArray> bytes(input);std::vector<char*> args;
    for(auto& b:bytes)args.push_back(b.data());
    return remoteDiagnosticsConfigFromProcess(int(args.size()),args.data(),"test");
}
void write(const QString& path,const QByteArray& bytes) {QFile f(path);REQUIRE(f.open(QIODevice::WriteOnly));REQUIRE(f.write(bytes)==bytes.size());}
bool waitFor(const std::function<bool()>& condition) {
    QElapsedTimer timer;timer.start();
    while(!condition()&&timer.elapsed()<3000){QCoreApplication::processEvents();QThread::msleep(1);}
    return condition();
}
}
TEST_CASE("Diagnostics defaults are configured but consent remains explicit") {
    auto c=config({"test"});CHECK_FALSE(c.enabled);
    CHECK(c.endpoint.toString()=="https://gearsqueens.online/sdr-town-diag/ingest");
    QTemporaryDir dir;const auto yes=dir.filePath("yes.json"),no=dir.filePath("no.json");
    write(yes,R"({"enabled":true,"url":"https://collector.invalid/ingest"})");
    write(no,R"({"enabled":false})");
    CHECK_FALSE(config({"test","--diag-config",yes.toUtf8(),"--diag-config",no.toUtf8()}).enabled);
    CHECK(config({"test","--diag-config",no.toUtf8(),"--diag-config",yes.toUtf8()}).enabled);
    CHECK_FALSE(config({"test","--no-remote-diagnostics","--diag-url","https://collector.invalid/ingest"}).enabled);
    CHECK_FALSE(config({"test","--diag-url","http://collector.invalid/ingest","--diag-token","fixture"}).enabled);
    QSettings settings;
    settings.setValue("remoteDiagnostics/consent", true);
    CHECK(config({"test"}).enabled);
    CHECK_FALSE(config({"test", "--diag-off"}).enabled);
    settings.setValue("remoteDiagnostics/consent", false);
    CHECK_FALSE(config({"test", "--diag-url", "https://collector.invalid/ingest"}).enabled);
    settings.remove("remoteDiagnostics/consent");
}
TEST_CASE("Diagnostics require an actual acknowledgement and recover after stalled transport") {
    QTcpServer server;REQUIRE(server.listen(QHostAddress::LocalHost));
    int requests=0;bool stall=false;bool validAck=true;
    QObject::connect(&server,&QTcpServer::newConnection,&server,[&] {
        while(auto* socket=server.nextPendingConnection()) {
            QObject::connect(socket,&QTcpSocket::readyRead,socket,[&,socket] {
                auto data=socket->property("input").toByteArray()+socket->readAll();socket->setProperty("input",data);
                const int boundary=data.indexOf("\r\n\r\n");if(boundary<0||socket->property("handled").toBool())return;
                int length=0;
                for(const auto& line:data.left(boundary).split('\n'))
                    if(line.toLower().startsWith("content-length:"))length=line.mid(15).trimmed().toInt();
                if(data.size()<boundary+4+length)return;
                socket->setProperty("handled",true);++requests;
                CHECK(QJsonDocument::fromJson(data.mid(boundary+4,length)).isObject());
                if(stall)return;
                const QByteArray body=validAck?"{\"ok\":true}":"{\"ok\":false}";
                socket->write("HTTP/1.1 202 Accepted\r\nConnection: close\r\nContent-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body);
                socket->disconnectFromHost();
            });
        }
    });
    RemoteDiagnosticsConfig c;c.enabled=true;c.endpoint=QUrl("http://127.0.0.1:"+QString::number(server.serverPort())+"/ingest");
    c.minIntervalMs=100;c.requestTimeoutMs=200;
    RemoteDiagnosticsClient client;client.configure(c);
    client.submit("test","info",{{"fixture",true}});
    REQUIRE(waitFor([&]{return client.deliveryStatistics()["acknowledged"].toInt()==1;}));
    validAck=false;client.submit("test","info",{});
    REQUIRE(waitFor([&]{return client.deliveryStatistics()["networkDropped"].toInt()==1;}));
    stall=true;client.submit("test","info",{});
    REQUIRE(waitFor([&]{return client.deliveryStatistics()["networkDropped"].toInt()==2;}));
    stall=false;validAck=true;client.submit("test","info",{});
    REQUIRE(waitFor([&]{return client.deliveryStatistics()["acknowledged"].toInt()==2;}));CHECK(requests==4);
}
TEST_CASE("CPU capacity and process resource sampling are explicit measurements") {
    CHECK(ProcessPerformance::cpuPercent(1,1,4)==25);
    CHECK(ProcessPerformance::cpuPercent(4,1,4)==100);
    CHECK(ProcessPerformance::cpuPercent(-1,1,4)==0);
    CHECK(ProcessPerformance::cpuPercent(1,0,4)==0);
    ProcessPerformance p;auto first=p.sample();CHECK_FALSE(first.contains("cpuCapacityPercent"));
    CHECK(first["logicalCpus"].toInt()>0);auto next=p.sample();CHECK(next.contains("processCpuSeconds"));
#ifdef _WIN32
    CHECK(next["workingSetBytes"].toDouble()>0);CHECK(next["threadCount"].toInt()>0);
#endif
}

TEST_CASE("Diagnostics menu cancellation and opt-out preserve explicit consent") {
    QSettings settings;settings.remove("remoteDiagnostics/consent");
    QMainWindow window;window.menuBar()->addMenu("&Help");
    installDiagnosticsMenu(window);
    auto* action=window.findChild<QAction*>("diagnosticsConsentAction");
    REQUIRE(action);CHECK(action->isCheckable());CHECK_FALSE(action->isChecked());
    QTimer::singleShot(0, [] {
        if(auto* message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))message->done(QMessageBox::No);
    });
    action->trigger();CHECK_FALSE(action->isChecked());
    CHECK_FALSE(settings.contains("remoteDiagnostics/consent"));
    action->setChecked(true);action->trigger();
    CHECK_FALSE(action->isChecked());CHECK_FALSE(settings.value("remoteDiagnostics/consent").toBool());
    settings.remove("remoteDiagnostics/consent");
}
