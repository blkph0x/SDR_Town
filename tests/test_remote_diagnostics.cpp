#include "RemoteDiagnostics.h"
#include "FmDiagnosticsLog.h"
#include "DiagnosticsMenu.h"
#include "DiagnosticsObserver.h"
#include "DiagnosticsHealthMonitor.h"
#include <QPushButton>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <thread>
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
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QUuid>
#include <QSettings>
#include <vector>

TEST_CASE("FM diagnostics are numeric bounded idle-aware local reports", "[fm][diagnostics]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    FmDiagnosticsLog log(directory.path());
    log.sample(); // Ignore counters from other tests in this process.
    REQUIRE(log.sample().isEmpty());
    {
        fmDiagnostics::Block block(false,24000,2400000);
        block.values[fmDiagnostics::DiscSamples]=462;
        block.values[fmDiagnostics::AudioSamples]=480;
        block.values[fmDiagnostics::LookaheadReads]=2;
    }
    const auto report=log.sample();
    REQUIRE_FALSE(report.isEmpty());
    REQUIRE_FALSE(report.contains("localWriteFailed"));
    REQUIRE(report["modes"].toObject()["nfm"].toObject().contains("resamplerLookaheadReads"));
    REQUIRE(report["modes"].toObject()["nfm"].toObject()["audioSamples"].toDouble()>=480);
    REQUIRE(log.sample().isEmpty());
    QFile file(log.path());REQUIRE(file.open(QIODevice::ReadOnly));
    const auto text=file.readAll();file.close();
    REQUIRE(text.contains("discriminatorSamples"));
    REQUIRE_FALSE(text.contains("frequency"));
    REQUIRE(file.open(QIODevice::Append));file.write(QByteArray(1024*1024,' '));file.close();
    {fmDiagnostics::Block block(true,1920,192000);}
    REQUIRE_FALSE(log.sample().contains("localWriteFailed"));
    REQUIRE(QFile::exists(log.path()+".1"));
    REQUIRE(QFile(log.path()).size()<4096);
}

TEST_CASE("FM diagnostics retain active logs and report local write failures", "[fm][diagnostics]") {
    QTemporaryDir dir;REQUIRE(dir.isValid());
    for(int i=0;i<12;++i) {
        QFile f(dir.filePath("fm-old"+QString::number(i)+".jsonl"));
        REQUIRE(f.open(QIODevice::WriteOnly));f.write("{}\n");
    }
    QLockFile active(dir.filePath("fm-old0.jsonl.lock"));REQUIRE(active.tryLock(0));
    FmDiagnosticsLog log(dir.path());
    REQUIRE(QFile::exists(dir.filePath("fm-old0.jsonl")));
    REQUIRE(QDir(dir.path()).entryList({"*.jsonl"},QDir::Files).size()<=9);
    FmDiagnosticsLog duplicate(dir.path()); // Cannot take the active writer's lock.
    {fmDiagnostics::Block block(false,480,48000);}
    REQUIRE(duplicate.sample()["localWriteFailed"].toBool());
    REQUIRE_FALSE(log.sample().contains("localWriteFailed"));
}

TEST_CASE("FM diagnostics worker starts once and joins on owner destruction", "[fm][diagnostics]") {
    auto owner=std::make_unique<QObject>();
    startFmDiagnostics(owner.get());startFmDiagnostics(owner.get());
    REQUIRE(owner->findChildren<QThread*>().size()==1);
    {fmDiagnostics::Block block(false,480,48000);}
    owner.reset(); // Must stop and join even if the thread is only just starting.
}

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
    QJsonObject received;
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
                received=QJsonDocument::fromJson(data.mid(boundary+4,length)).object();
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
    client.submit("test","info",{{"fixture",true},{"iq",QJsonArray{1,2}},
        {"audio",QJsonObject{{"engineCreated",true},{"masterVolume",0.85},{"underruns",3},
            {"activeOutputNames","Fixture speaker"},{"pcm",QJsonArray{1,2}},{"secret","never-upload"}}}});
    REQUIRE(waitFor([&]{return client.deliveryStatistics()["acknowledged"].toInt()==1;}));
    const auto payload=received["payload"].toObject();
    CHECK_FALSE(payload.contains("iq"));
    const auto audio=payload["audio"].toObject();CHECK(audio["engineCreated"].toBool());
    CHECK(audio["underruns"].toInt()==3);CHECK(audio["masterVolume"].toDouble()==0.85);
    CHECK(audio["activeOutputNames"].toString()=="Fixture speaker");
    CHECK_FALSE(audio.contains("pcm"));CHECK_FALSE(audio.contains("secret"));
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

TEST_CASE("Health monitoring follows late opt-in and resets session timing", "[health]") {
    QObject owner; QString session;
    int starts = 0, stalls = 0, pressure = 0; qint64 lastStall = 0;
    DiagnosticsHealthMonitor monitor(&owner, [&] { return session; }, [&] { ++starts; },
        [&](qint64 ms) { ++stalls; lastStall = ms; }, [&] { ++pressure; return true; });
    monitor.poll(0); monitor.poll(600000);
    CHECK(starts == 0); CHECK(stalls == 0); CHECK(pressure == 0);
    session = "first"; monitor.poll(700000); CHECK(starts == 1);
    monitor.poll(701000); CHECK(stalls == 0);
    monitor.poll(710000); CHECK(stalls == 1); CHECK(lastStall == 9000);
    monitor.poll(720000); CHECK(stalls == 1);
    monitor.poll(760000); CHECK(pressure == 1);
    session.clear(); monitor.poll(2000000);
    CHECK(pressure == 1); CHECK(stalls == 1);
    session = "second"; monitor.poll(3000000); CHECK(starts == 2);
    monitor.poll(3001000); CHECK(stalls == 1); CHECK(pressure == 1);
    monitor.poll(3060000); CHECK(stalls == 2); CHECK(pressure == 2);
}

TEST_CASE("Diagnostic status replies are bounded and consent cancellation discards callbacks", "[status]") {
    QTcpServer server; REQUIRE(server.listen(QHostAddress::LocalHost));
    QByteArray body = "{\"ok\":true}";
    bool trickle = false;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (auto* socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                socket->readAll();
                if (socket->property("answered").toBool()) return;
                socket->setProperty("answered", true);
                if (trickle) {
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: 1000000\r\n\r\n");
                    auto* timer = new QTimer(socket);
                    QObject::connect(timer, &QTimer::timeout, socket, [socket] { socket->write(" "); });
                    timer->start(20);
                } else {
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    socket->disconnectFromHost();
                }
            });
        }
    });
    RemoteDiagnosticsConfig cfg; cfg.enabled = true; cfg.requestTimeoutMs = 150;
    cfg.endpoint = QUrl("http://127.0.0.1:" + QString::number(server.serverPort()) + "/ingest");
    RemoteDiagnosticsClient client; client.configure(cfg);
    QObject context; int replies = 0; QJsonObject result;
    const auto callback = [&](const QJsonObject& value) { result = value; ++replies; };
    SECTION("valid status") {
        client.checkClientStatus(&context, callback);
        REQUIRE(waitFor([&] { return replies == 1; })); CHECK(result["ok"].toBool());
    }
    SECTION("oversize valid JSON must not pass") {
        body = "{\"ok\":true,\"padding\":\"" + QByteArray(256 * 1024, 'x') + "\"}";
        client.checkClientStatus(&context, callback);
        REQUIRE(waitFor([&] { return replies == 1; })); CHECK_FALSE(result["ok"].toBool());
    }
    SECTION("trickled bytes cannot extend the absolute deadline") {
        trickle = true;
        client.checkClientStatus(&context, callback);
        REQUIRE(waitFor([&] { return replies == 1; })); CHECK_FALSE(result["ok"].toBool());
    }
    SECTION("opt-out discards a pending status") {
        client.checkClientStatus(&context, callback);
        client.stopWithoutSending();
        QCoreApplication::processEvents(); QCoreApplication::processEvents();
        CHECK(replies == 0);
    }
    SECTION("one status request at a time and reconfiguration invalidates old replies") {
        trickle = true;
        int retiredReplies = 0;
        client.checkClientStatus(&context, [&](const auto&) { ++retiredReplies; });
        client.checkClientStatus(&context, callback);
        REQUIRE(waitFor([&] { return replies == 1; }));
        CHECK(result["error"].toString() == "status-request-in-progress");
        client.configure(cfg);
        CHECK_FALSE(client.deliveryStatistics()["statusInFlight"].toBool());
        trickle = false;
        client.checkClientStatus(&context, callback);
        REQUIRE(waitFor([&] { return replies == 2; }));
        CHECK(result["ok"].toBool());
        CHECK(retiredReplies == 0);
        CHECK(client.deliveryStatistics()["statusFailures"].toInt() == 0);
    }
    SECTION("client destruction discards pending callbacks") {
        auto temporary = std::make_unique<RemoteDiagnosticsClient>();
        temporary->configure(cfg);
        temporary->checkClientStatus(&context, callback);
        temporary.reset();
        QCoreApplication::processEvents(); QCoreApplication::processEvents();
        CHECK(replies == 0);
    }
}

TEST_CASE("Opted-in GUI actions are bounded private and session scoped") {
    QObject owner;QWidget panel;
    QPushButton tune("Set/Tune Device",&panel),secret("private-call-sign",&panel);
    secret.setObjectName("credential-do-not-send");
    QLineEdit input("password-do-not-send",&panel);
    QDoubleSpinBox number(&panel);number.setRange(0,1000);number.setValue(420.35);
    QString session;int snapshots=0;
    QList<QPair<QString,QJsonObject>> reports;
    DiagnosticsObserver observer(&owner,[&]{++snapshots;return QJsonObject{{"fixture",true}};},
        [&]{return session;},[&](const QString& type,const QJsonObject& value){if(type!="ui.intent")reports.append({type,value});});
    observer.observe(&panel);observer.observe(&panel);
    tune.click();observer.tick();CHECK(reports.isEmpty());CHECK(snapshots==0);
    session="first";observer.tick();REQUIRE(reports.size()==1);CHECK(snapshots==1);
    reports.clear();tune.click();secret.click();number.editingFinished();input.setText("other-secret");
    observer.tick();observer.tick();
    REQUIRE(reports.size()==2);
    const auto actions=reports[0].second["actions"].toArray();REQUIRE(actions.size()==3);
    CHECK(actions[0].toObject()["command"]=="Set/Tune Device");
    CHECK(actions[2].toObject()["value"].toDouble()==420.35);
    const auto encoded=QJsonDocument(reports[0].second).toJson();
    CHECK_FALSE(encoded.contains("private-call"));CHECK_FALSE(encoded.contains("credential"));
    CHECK_FALSE(encoded.contains("password"));CHECK_FALSE(encoded.contains("other-secret"));
    reports.clear();for(int i=0;i<100;++i)tune.click();observer.tick();observer.tick();
    REQUIRE(reports.size()==2);CHECK(reports[0].second["actions"].toArray().size()==32);
    CHECK(reports[0].second["dropped"].toInt()==68);
    reports.clear();tune.click();session.clear();observer.tick();CHECK(reports.isEmpty());
    session="second";observer.tick();REQUIRE(reports.size()==1);CHECK(reports[0].first=="app.runtime");
    QFileDialog file;QPushButton fileButton("Apply",&file);observer.observe(&file);
    reports.clear();fileButton.click();observer.tick();observer.tick();CHECK(reports.isEmpty());
}

TEST_CASE("Diagnostic ingress and queues retain critical context under decoder floods") {
    RemoteDiagnosticsConfig c;c.enabled=true;c.endpoint=QUrl("http://127.0.0.1:1/ingest");c.maxQueue=4;
    RemoteDiagnosticsClient client;client.configure(c);
    client.submit("app.system","info",{{"cpuModel","fixture"}});
    client.submit("app.runtime","info",{{"fixture",true}});
    for(int i=0;i<1000;++i) client.submit("p25.voice","info",{{"counter",i}});
    auto stats=client.deliveryStatistics();CHECK(stats["queued"].toInt()==3);
    CHECK(stats["coalesced"].toInt()==999);CHECK(stats["queueDropped"].toInt()==0);
    std::thread producer([&]{for(int i=0;i<5000;++i)client.submit("p25.log","info",{{"counter",i}});});
    producer.join();CHECK(client.deliveryStatistics()["ingressDropped"].toInt()==4999);
    client.stopWithoutSending();QCoreApplication::processEvents();
    CHECK(client.deliveryStatistics()["queued"].toInt()==0);
    CHECK(client.deliveryStatistics()["acknowledged"].toInt()==0);
    client.submit("app.runtime","info",{});CHECK(client.deliveryStatistics()["queued"].toInt()==0);
}

TEST_CASE("Startup system specifications omit personal machine identifiers") {
    auto info=ProcessPerformance::systemInfo();
    REQUIRE(info["logicalCpus"].toInt()>0);REQUIRE_FALSE(info["os"].toString().isEmpty());
    CHECK_FALSE(info.contains("hostname"));CHECK_FALSE(info.contains("username"));
    CHECK_FALSE(info.contains("machineId"));CHECK_FALSE(info.contains("ip"));
#ifdef _WIN32
    CHECK(info["physicalTotalBytes"].toDouble()>0);CHECK_FALSE(info["cpuModel"].toString().isEmpty());
#endif
}
