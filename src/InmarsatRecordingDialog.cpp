#include "InmarsatDiagnosticRecording.h"
#include "RemoteDiagnostics.h"
#include <QCoreApplication>
#include <QDialog>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QTimer>
#include <QVBoxLayout>
#include <vector>
#include <memory>
#include <algorithm>

void showInmarsatDiagnosticRecording(QWidget* parent,double channelHz) {
    static QPointer<QDialog> existing;
    if(existing) {existing->show();existing->raise();return;}
    auto* dialog=new QDialog(parent);existing=dialog;dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("Inmarsat diagnostic recording");dialog->resize(500,340);
    auto* layout=new QVBoxLayout(dialog);
    auto* frequency=new QDoubleSpinBox;frequency->setDecimals(6);frequency->setRange(1,10000);
    frequency->setSuffix(" MHz");frequency->setValue(channelHz/1e6);
    auto* form=new QFormLayout;form->addRow("Channel",frequency);layout->addLayout(form);
    auto* consent=new QCheckBox("I agree to record signal and decoded voice for diagnosis");
    consent->setObjectName("inmarsatRecordingConsent");layout->addWidget(consent);
    consent->setToolTip("Five seconds of modem input and decoded audio may contain private voice, aircraft IDs and location data. Recording does not send anything.");
    auto* capture=new QPushButton("Record 5 seconds");layout->addWidget(capture);
    capture->setObjectName("recordingStart");capture->setEnabled(false);
    auto* cancel=new QPushButton("Discard");layout->addWidget(cancel);
    auto* status=new QLabel("Idle");status->setWordWrap(true);layout->addWidget(status);
    auto* save=new QPushButton("Save recording...");layout->addWidget(save);
    auto* send=new QPushButton("Review and send...");layout->addWidget(send);
    save->setEnabled(false);send->setEnabled(false);
    auto* network=new QNetworkAccessManager(dialog);
    auto uploading=std::make_shared<bool>(false);
    QObject::connect(capture,&QPushButton::clicked,dialog,[=] {
        if(consent->isChecked())InmarsatDiagnosticRecording::arm(frequency->value()*1e6);
    });
    QObject::connect(cancel,&QPushButton::clicked,dialog,[]{InmarsatDiagnosticRecording::cancel();});
    QObject::connect(consent,&QCheckBox::toggled,dialog,[](bool on){if(!on)InmarsatDiagnosticRecording::cancel();});
    QObject::connect(dialog,&QObject::destroyed,[]{InmarsatDiagnosticRecording::cancel();});
    QObject::connect(save,&QPushButton::clicked,dialog,[=] {
        const auto body=InmarsatDiagnosticRecording::bundle();if(body.isEmpty())return;
        const auto path=QFileDialog::getSaveFileName(dialog,"Save diagnostic recording",{},"Inmarsat recording (*.inmarsat.json)");
        if(path.isEmpty())return;
        QSaveFile file(path);
        if(!file.open(QIODevice::WriteOnly) || file.write(body)!=body.size() || !file.commit())
            QMessageBox::warning(dialog,"Save failed","Could not save recording.");
    });
    QObject::connect(send,&QPushButton::clicked,dialog,[=] {
        if(*uploading || !consent->isChecked())return;
        auto args=QCoreApplication::arguments();
        if(args.contains("--no-remote-diagnostics") || args.contains("--diag-off") || args.contains("--diagnostics-off")) {
            QMessageBox::warning(dialog,"Upload disabled","Remote diagnostics were disabled at startup.");return;
        }
        std::vector<QByteArray> bytes;for(const auto& arg:args)bytes.push_back(arg.toLocal8Bit());
        std::vector<char*> argv;for(auto& arg:bytes)argv.push_back(arg.data());
        auto cfg=remoteDiagnosticsConfigFromProcess(int(argv.size()),argv.data(),"inmarsat-recording");
        if(cfg.endpoint.scheme()!="https" || cfg.endpoint.host().isEmpty() || cfg.bearerToken.isEmpty() || !cfg.endpoint.path().endsWith("/ingest")) {
            QMessageBox::warning(dialog,"Upload unavailable","A valid HTTPS diagnostics collector and restricted credential are required.");return;
        }
        auto object=QJsonDocument::fromJson(InmarsatDiagnosticRecording::bundle()).object();if(object.isEmpty())return;
        object["clientId"]=remoteDiagnosticsClientId();
        if(object["clientId"].toString().isEmpty()) {
            QMessageBox::warning(dialog,"Diagnostics disabled","Enable remote diagnostics in the app menu before sending a recording.");return;
        }
        object["version"]=QCoreApplication::applicationVersion();
        const auto body=QJsonDocument(object).toJson(QJsonDocument::Compact);
        if(body.size()>1024*1024)return;
        QUrl url=cfg.endpoint;auto path=url.path();path.chop(7);url.setPath(path+"/recordings");url.setQuery({});url.setFragment({});
        if(QMessageBox::question(dialog,"Send private diagnostic recording?",
            QString("Send %1 KiB to %2?\n\nContains five seconds of modem signal, decoded voice, and %3 ms of original IQ at %4 samples/sec, plus channel frequency, time, app version and installation ID. These may reveal aircraft identities, locations or private speech. Only send recordings you are authorized to share.\n\nStored for up to 30 days; at most 15 uploads per installation per rolling 24 hours. No automatic retries. Ordinary telemetry remains counters-only.")
                .arg((body.size()+1023)/1024).arg(url.toDisplayString(QUrl::RemoveUserInfo))
                .arg(1000.0*QByteArray::fromBase64(object["iqBase64"].toString().toLatin1()).size()/8/std::max(1.0,object["iqRate"].toDouble()),0,'f',2)
                .arg(object["iqRate"].toDouble(),0,'f',0),
            QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
        QNetworkRequest request(url);request.setTransferTimeout(30000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
        request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
        request.setRawHeader("Authorization","Bearer "+cfg.bearerToken.toUtf8());
        *uploading=true;status->setText("Sending...");
        auto* reply=network->post(request,body);
        QTimer::singleShot(30000,reply,[reply]{if(!reply->isFinished())reply->abort();});
        QObject::connect(reply,&QNetworkReply::readyRead,reply,[reply]{if(reply->bytesAvailable()>4096)reply->abort();});
        QObject::connect(consent,&QCheckBox::toggled,reply,[reply](bool on){if(!on)reply->abort();});
        QObject::connect(reply,&QNetworkReply::finished,dialog,[=] {
            *uploading=false;
            const auto ack=QJsonDocument::fromJson(reply->readAll()).object();
            const int httpStatus=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool ok=reply->error()==QNetworkReply::NoError && httpStatus==201 && ack["ok"].toBool();
            const QString failure=httpStatus==429
                ? "Collector upload limit reached. The allowance is 15 recordings per installation per rolling 24 hours; request-rate and total-storage limits also apply. Save this recording locally, then retry later or contact the administrator. It has not been received."
                : "The server did not acknowledge this recording. It remains available locally; no automatic retry will occur.";
            QMessageBox::information(dialog,ok?"Recording received":"Upload failed",
                ok?"Server receipt: "+ack["recordingId"].toString():failure);
            reply->deleteLater();
        });
    });
    auto* timer=new QTimer(dialog);QObject::connect(timer,&QTimer::timeout,dialog,[=] {
        const auto state=InmarsatDiagnosticRecording::status();if(!*uploading)status->setText(state);
        const bool active=state.startsWith("Waiting") || state.startsWith("Recording");
        capture->setEnabled(consent->isChecked() && !active && !*uploading);
        frequency->setEnabled(!active && !*uploading);cancel->setEnabled(!*uploading);
        save->setEnabled(state=="Ready for review" && !*uploading);
        send->setEnabled(consent->isChecked() && state=="Ready for review" && !*uploading);
    });timer->start(100);dialog->show();
}
