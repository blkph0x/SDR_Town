#include "InmarsatReplayCommand.h"
#include "InmarsatReplayDialog.h"
#include "InmarsatDiagnostics.h"
#include "RemoteDiagnostics.h"
#include "InmarsatAero.h"
#include "InmarsatAudio.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>
#include <QCoreApplication>
#include <QEventLoop>
#include <QSaveFile>
#include <QTimer>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

int runInmarsatReplayAutomation(bool gui) {
    connectInmarsatRemoteDiagnostics();
    InmarsatReplayOptions options;
    options.shareDiagnostics = remoteDiagnosticsEnabled();
    QString resultPath;
    bool exitComplete = !gui;
    try {
        const auto args = QCoreApplication::arguments();
        for (int i = 1; i < args.size(); ++i) {
            const auto key = args[i];
            auto value = [&]() {
                if (++i >= args.size() || args[i].startsWith("--")) throw std::runtime_error("Missing Inmarsat argument value");
                return args[i];
            };
            auto number = [&]() {
                bool ok = false;
                const double n = value().toDouble(&ok);
                if (!ok || !std::isfinite(n) || n < 0) throw std::runtime_error("Invalid Inmarsat numeric argument");
                return n;
            };
            if (key == "--inmarsat-iq") options.path = value();
            else if (key == "--inmarsat-format") options.input.format = value();
            else if (key == "--inmarsat-rate") options.input.sampleRateHz = number();
            else if (key == "--inmarsat-center") options.input.centerHz = number();
            else if (key == "--inmarsat-channel") options.channelHz = number();
            else if (key == "--inmarsat-fast") options.realTime = false;
            else if (key == "--inmarsat-play-audio") options.playAudio = true;
            else if (key == "--inmarsat-wav") options.wavPath = value();
            else if (key == "--inmarsat-exit-complete") exitComplete = true;
            else if (key == "--inmarsat-result") resultPath = value();
            else if (key == "--inmarsat-log-dir") options.logDirectory = value();
            else if (key == "--inmarsat-mode") {
                const auto mode = value();
                if (mode == "600") options.mode = InmarsatDemodMode::AeroMsk600;
                else if (mode == "1200") options.mode = InmarsatDemodMode::AeroMsk1200;
                else if (mode == "8400") options.mode = InmarsatDemodMode::AeroVoice8400;
                else if (mode == "10500") options.mode = InmarsatDemodMode::AeroOqpsk10500;
                else if (mode == "1200-burst") options.mode = InmarsatDemodMode::AeroBurstMsk1200;
                else if (mode == "10500-burst") options.mode = InmarsatDemodMode::AeroBurstOqpsk10500;
                else if (mode == "egc") options.mode = InmarsatDemodMode::EgcBpsk1200;
                else throw std::runtime_error("Inmarsat mode must be 600, 1200, 8400, 10500, 1200-burst, 10500-burst or egc");
            } else if (key.startsWith("--inmarsat-") && key != "--inmarsat-replay") {
                throw std::runtime_error("Unknown Inmarsat replay argument");
            }
        }
        if (options.path.isEmpty() && (!gui || exitComplete))
            throw std::runtime_error("--inmarsat-iq is required for automated replay");
        if(options.input.format=="diagnostic_if") {
            if(gui)throw std::runtime_error("Diagnostic IF reproduction currently requires --cli");
            QFile file(options.path);
            if(!file.open(QIODevice::ReadOnly) || file.size()>1024*1024)
                throw std::runtime_error("Cannot open bounded diagnostic recording");
            const auto object=QJsonDocument::fromJson(file.readAll()).object();
            const auto encoded=object["ifBase64"].toString().toLatin1();
            const auto decoded=QByteArray::fromBase64Encoding(encoded,QByteArray::AbortOnBase64DecodingErrors);
            const int mode=object["mode"].toInt(-1);
            if(object["schema"].toString()!="sdr-town-inmarsat-recording-v1" ||
                object["format"].toString()!="s16le" || object["ifRate"].toInt()!=48000 ||
                !decoded || decoded.decoded.size()!=480000 || mode<0 || mode>6 || mode==4)
                throw std::runtime_error("Invalid diagnostic modem recording");
            const int rate=mode==0?600:(mode==1 || mode==5)?1200:mode==3?8400:10500;
            InmarsatAero decoder(rate,mode==5 || mode==6);
            InmarsatAudio audio(false,options.wavPath);
            decoder.setPcmSink([&](std::span<const int16_t> pcm,uint32_t aes){audio.push(pcm,aes);});
            std::vector<int16_t> samples(decoded.decoded.size()/2);
            for(size_t i=0;i<samples.size();++i)samples[i]=qFromLittleEndian<int16_t>(decoded.decoded.constData()+2*i);
            for(size_t i=0;i<samples.size();i+=480)
                decoder.processIf(std::span<const int16_t>(samples).subspan(i,std::min<size_t>(480,samples.size()-i)));
            audio.finish();const auto stats=decoder.stats();
            nlohmann::json report={{"schema","inmarsat-if-reproduction-v1"},
                {"input48k",stats.input48k},{"crcOk",stats.crcOk},{"crcBad",stats.crcBad},
                {"pcmSamples",stats.pcmSamples},{"audio",audio.report()},
                {"coldStart",true},{"continuousAudioVerified",false}};
            const auto json=QByteArray::fromStdString(report.dump(2));std::cout<<json.constData()<<'\n';
            if(!resultPath.isEmpty()) {
                QSaveFile output(resultPath);
                if(!output.open(QIODevice::WriteOnly) || output.write(json)!=json.size() || !output.commit())
                    throw std::runtime_error("Cannot save diagnostic reproduction report");
            }
            return 0;
        }
        InmarsatReplay cliReplay;
        std::unique_ptr<InmarsatReplayDialog> dialog;
        QEventLoop loop;
        QTimer poll;
        InmarsatReplaySnapshot result;
        if (gui) {
            dialog = std::make_unique<InmarsatReplayDialog>();
            configureInmarsatReplaySharing(*dialog);
            QObject::connect(dialog.get(), &QDialog::finished, &loop, &QEventLoop::quit);
            dialog->show();
            if (!options.path.isEmpty()) dialog->startReplay(options);
        } else cliReplay.start(options);
        QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
            result = gui ? dialog->snapshot() : cliReplay.snapshot();
            if (exitComplete && !result.running) loop.quit();
        });
        poll.start(50);
        loop.exec();
        cliReplay.stop();
        if (dialog) dialog->stopReplay();
        result = gui ? dialog->snapshot() : cliReplay.snapshot();
        dialog.reset();
        const auto json = QByteArray::fromStdString(result.toJson().dump(2));
        std::cout << json.constData() << '\n';
        if (!resultPath.isEmpty()) {
            QSaveFile file(resultPath);
            if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit())
                throw std::runtime_error("Cannot save Inmarsat replay result");
        }
        // Exit 0 proves a completed input/diagnostics run, never successful voice decoding.
        return result.state == "error" || !result.logError.isEmpty() ? 2 : 0;
    } catch (const std::exception& e) {
        std::cerr << "Inmarsat replay: " << e.what() << '\n';
        return 2;
    }
}
