#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QStatusBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QPushButton>
#include <QTextEdit>
#include <QPalette>
#include <QStyleFactory>
#include <QMessageBox>
#include <QAction>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>
#include <QDialog>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QGroupBox>
#include <QFormLayout>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <nlohmann/json.hpp>

#include "DeviceManager.h"

using json = nlohmann::json;

void setupLogging()
{
    try {
        const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(appData + "/logs");

        const std::string logPath = (appData + "/logs/maulaudio.log").toStdString();

        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logPath, 1024*1024*5, 3);

        std::vector<spdlog::sink_ptr> sinks {console_sink, file_sink};
        auto logger = std::make_shared<spdlog::logger>("maulaudio", sinks.begin(), sinks.end());
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::info);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        spdlog::info("MaulAudio Pro logging initialized. Log file: {}", logPath);
    } catch (const std::exception& ex) {
        qWarning() << "Failed to initialize logging:" << ex.what();
    }
}

void applyDarkTheme(QApplication& app)
{
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(45, 45, 48));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(30, 30, 32));
    darkPalette.setColor(QPalette::AlternateBase, QColor(45, 45, 48));
    darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, QColor(60, 60, 63));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);

    app.setPalette(darkPalette);

    app.setStyleSheet(
        "QMenuBar { background-color: #2d2d30; color: white; }"
        "QMenuBar::item:selected { background-color: #3e3e42; }"
        "QMenu { background-color: #2d2d30; color: white; border: 1px solid #3e3e42; }"
        "QMenu::item:selected { background-color: #3e3e42; }"
        "QStatusBar { background-color: #2d2d30; color: #cccccc; }"
        "QToolTip { color: #ffffff; background-color: #2d2d30; border: 1px solid #3e3e42; }"
    );
}

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(QWidget* parent = nullptr) : QMainWindow(parent)
    {
        setWindowTitle("MaulAudio Pro");
        resize(1280, 800);

        // Central placeholder (will become spectrum + receivers etc.)
        QWidget* central = new QWidget(this);
        QVBoxLayout* layout = new QVBoxLayout(central);

        QLabel* title = new QLabel("MaulAudio Pro", this);
        title->setStyleSheet("font-size: 28px; font-weight: bold; color: #4fc3f7; margin: 20px;");
        title->setAlignment(Qt::AlignCenter);

        QLabel* subtitle = new QLabel(
            "Professional Multi-SDR Monitor • Smart Scanner • Voice & Data • Advanced Signal Analysis",
            this);
        subtitle->setStyleSheet("font-size: 14px; color: #aaaaaa; margin-bottom: 30px;");
        subtitle->setAlignment(Qt::AlignCenter);

        QLabel* statusLabel = new QLabel(
            "PR 1 + PR 2 progress: Foundation + Device Layer (SoapySDR enumeration + config dialog with persistence).\n\n"
            "Open Devices → Device Manager to see/enable/configure devices (real or stubs).\n"
            "Next: PR 3 visualization, PR 4 multi-device audio (speakers + VB-Audio Cable).\n\n"
            "See DESIGN.md (root) for the complete approved plan and roadmap.",
            this);
        statusLabel->setStyleSheet("font-size: 13px; color: #cccccc;");
        statusLabel->setAlignment(Qt::AlignCenter);
        statusLabel->setWordWrap(true);

        QPushButton* aboutBtn = new QPushButton("About / Legal Notice", this);
        connect(aboutBtn, &QPushButton::clicked, this, &MainWindow::showAbout);

        layout->addStretch();
        layout->addWidget(title);
        layout->addWidget(subtitle);
        layout->addWidget(statusLabel);
        layout->addSpacing(30);
        layout->addWidget(aboutBtn, 0, Qt::AlignCenter);
        layout->addStretch();

        setCentralWidget(central);

        createMenus();

        // Initial device enumeration (PR2) for status
        auto& devMgr = DeviceManager::instance();
        auto initialDevs = devMgr.enumerateDevices();
        int enabled = 0;
        for (const auto& d : initialDevs) if (d.enabled) ++enabled;
        statusBar()->showMessage(QString("MaulAudio Pro — Professional SDR Tool  |  Devices: %1 total (%2 enabled)  |  Audio: not configured (PR4)").arg(initialDevs.size()).arg(enabled));
    }

private slots:
    void showAbout()
    {
        QMessageBox box(this);
        box.setWindowTitle("About MaulAudio Pro");
        box.setTextFormat(Qt::RichText);
        box.setText(
            "<b>MaulAudio Pro</b> — Professional multi-SDR monitoring and signal analysis.<br><br>"
            "This software is for <b>receiving only</b>. You are solely responsible for compliance "
            "with all applicable laws in your jurisdiction regarding radio reception, recording, "
            "and use of data.<br><br>"
            "<b>Important:</b> All decoding and analysis features are intended exclusively for "
            "<b>unencrypted / clear signals</b>. No decryption, cryptoanalysis, or attempts to "
            "access encrypted communications are implemented or supported. If a signal is encrypted, "
            "the output will be unintelligible or random.<br><br>"
            "Use responsibly and lawfully only.");
        box.setStandardButtons(QMessageBox::Ok);
        box.exec();
    }

    void onAudioConfig()
    {
        QMessageBox::information(this, "Audio",
            "Audio configuration dialog will be implemented in PR 4 (multi-device output with independent volumes for speakers + VB-Audio Cable etc.).\n\n"
            "See DESIGN.md section on Sound Output Configuration.");
    }

    void onDevices()
    {
        showDevicesDialog();
    }

    void showDevicesDialog()
    {
        QDialog dlg(this);
        dlg.setWindowTitle("Device Manager — MaulAudio Pro (PR 2)");
        dlg.resize(900, 520);

        auto& mgr = DeviceManager::instance();
        auto devs = mgr.enumerateDevices();

        QVBoxLayout* mainLay = new QVBoxLayout(&dlg);

        QLabel* hint = new QLabel("Rescan to refresh. Enable devices, adjust gain/sample rate/antenna. Settings persist across runs. Real SoapySDR + HackRF recommended (stubs shown if no Soapy).");
        hint->setWordWrap(true);
        mainLay->addWidget(hint);

        QTableWidget* table = new QTableWidget(devs.size(), 7, &dlg);
        QStringList headers = {"Enabled", "Label / Driver", "Serial", "Antenna", "Sample Rate (MS/s)", "Gain (dB)", "Freq Range (MHz)"};
        table->setHorizontalHeaderLabels(headers);
        table->horizontalHeader()->setStretchLastSection(true);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);

        std::vector<QCheckBox*> enableChecks;
        std::vector<QComboBox*> antCombos;
        std::vector<QDoubleSpinBox*> rateSpins;
        std::vector<QDoubleSpinBox*> gainSpins;

        for (size_t i = 0; i < devs.size(); ++i) {
            const auto& d = devs[i];
            int row = static_cast<int>(i);

            // Enabled
            QCheckBox* cb = new QCheckBox();
            cb->setChecked(d.enabled);
            table->setCellWidget(row, 0, cb);
            enableChecks.push_back(cb);

            // Label
            table->setItem(row, 1, new QTableWidgetItem(QString("%1 (%2)").arg(QString::fromStdString(d.label)).arg(QString::fromStdString(d.driver))));

            // Serial
            table->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(d.serial)));

            // Antenna combo
            QComboBox* ant = new QComboBox();
            for (const auto& a : d.antennas) ant->addItem(QString::fromStdString(a));
            if (!d.antenna.empty()) ant->setCurrentText(QString::fromStdString(d.antenna));
            table->setCellWidget(row, 3, ant);
            antCombos.push_back(ant);

            // Sample rate
            QDoubleSpinBox* rate = new QDoubleSpinBox();
            rate->setRange(0.1, 60.0);
            rate->setDecimals(3);
            rate->setSingleStep(0.1);
            rate->setSuffix(" MS/s");
            rate->setValue(d.sampleRate / 1e6);
            // add common rates from list if present
            table->setCellWidget(row, 4, rate);
            rateSpins.push_back(rate);

            // Gain
            QDoubleSpinBox* gain = new QDoubleSpinBox();
            gain->setRange(0, 80);
            gain->setDecimals(1);
            gain->setSingleStep(1);
            gain->setSuffix(" dB");
            gain->setValue(d.gain);
            table->setCellWidget(row, 5, gain);
            gainSpins.push_back(gain);

            // Freq range
            QString fr = QString("%1 – %2").arg(d.minFreq/1e6, 0, 'f', 0).arg(d.maxFreq/1e6, 0, 'f', 0);
            table->setItem(row, 6, new QTableWidgetItem(fr));
        }

        mainLay->addWidget(table);

        QHBoxLayout* btnLay = new QHBoxLayout();
        QPushButton* rescanBtn = new QPushButton("Rescan Devices");
        QPushButton* applyBtn = new QPushButton("Apply Changes");
        QPushButton* closeBtn = new QPushButton("Close");
        btnLay->addWidget(rescanBtn);
        btnLay->addStretch();
        btnLay->addWidget(applyBtn);
        btnLay->addWidget(closeBtn);
        mainLay->addLayout(btnLay);

        connect(rescanBtn, &QPushButton::clicked, [&]() {
            QMessageBox::information(&dlg, "Rescan", "Rescan will re-enumerate. Close and reopen the dialog (or restart app for full refresh in this build).");
        });

        connect(applyBtn, &QPushButton::clicked, [&]() {
            for (size_t i = 0; i < devs.size(); ++i) {
                mgr.setEnabled(i, enableChecks[i]->isChecked());

                double rateHz = rateSpins[i]->value() * 1e6;
                double g = gainSpins[i]->value();
                std::string ant = antCombos[i]->currentText().toStdString();

                mgr.updateDeviceParams(i, rateHz, g, ant);
            }
            mgr.saveSettings();
            statusBar()->showMessage(QString("Applied settings to %1 device(s)").arg(devs.size()), 3000);
            spdlog::info("Device settings applied from dialog.");
        });

        connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);

        dlg.exec();

        // Refresh main status
        int enabledCount = 0;
        for (const auto& d : mgr.getDevices()) if (d.enabled) ++enabledCount;
        statusBar()->showMessage(QString("Devices: %1 total, %2 enabled  |  See Device Manager dialog").arg(mgr.getDevices().size()).arg(enabledCount));
    }

private:
    void createMenus()
    {
        // File
        QMenu* fileMenu = menuBar()->addMenu("&File");
        fileMenu->addAction("E&xit", this, &QWidget::close);

        // Devices (stub)
        QMenu* devicesMenu = menuBar()->addMenu("&Devices");
        QAction* discoverAct = devicesMenu->addAction("&Rescan / Discover Devices...");
        connect(discoverAct, &QAction::triggered, this, &MainWindow::onDevices);
        devicesMenu->addSeparator();
        devicesMenu->addAction("Device &Manager...", this, &MainWindow::onDevices);

        // Receivers (stub)
        QMenu* rxMenu = menuBar()->addMenu("&Receivers");
        rxMenu->addAction("&Add Receiver...", [](){});
        rxMenu->addAction("&Receiver Table", [](){});

        // Scan (stub)
        QMenu* scanMenu = menuBar()->addMenu("&Scan");
        scanMenu->addAction("&Start Smart Scan", [](){});
        scanMenu->addAction("Band &Plans...", [](){});

        // Audio — important first-class menu (per design)
        QMenu* audioMenu = menuBar()->addMenu("&Audio");
        QAction* configAudio = audioMenu->addAction("Configure &Output Devices...");
        connect(configAudio, &QAction::triggered, this, &MainWindow::onAudioConfig);
        audioMenu->addSeparator();
        audioMenu->addAction("&Mute All Outputs", [](){});
        audioMenu->addAction("Test Tone (All)", [](){});

        // View / Tools / Settings (stubs)
        menuBar()->addMenu("&View");
        menuBar()->addMenu("&Tools");
        QMenu* settingsMenu = menuBar()->addMenu("&Settings");
        settingsMenu->addAction("Preferences...", [](){});

        // Help
        QMenu* helpMenu = menuBar()->addMenu("&Help");
        helpMenu->addAction("&About MaulAudio Pro", this, &MainWindow::showAbout);
        helpMenu->addAction("View &Design Document (DESIGN.md)", [this]() {
            // Simple hint — in real app we can open the file or a help viewer
            QMessageBox::information(this, "Design Document",
                "The full living design document is in the project root as DESIGN.md.\n"
                "It contains architecture, PR plan, key decisions, and detailed feature specs.\n\n"
                "Open it in any text editor or Markdown viewer.");
        });
    }
};

#include "main.moc"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("MaulAudio Pro");
    app.setOrganizationName("MaulAudio");
    app.setApplicationVersion("0.0.1-dev");

    setupLogging();
    applyDarkTheme(app);

    spdlog::info("Starting MaulAudio Pro v{}.", app.applicationVersion().toStdString());

    MainWindow w;
    w.show();

    spdlog::info("Main window shown. Entering Qt event loop.");
    int ret = app.exec();

    spdlog::info("Application exiting with code {}.", ret);
    spdlog::shutdown();
    return ret;
}
