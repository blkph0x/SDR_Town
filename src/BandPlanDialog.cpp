#include "BandPlanDialog.h"
#include "BandPlan.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFile>
#include <QSaveFile>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <stdexcept>

BandPlanDialog::BandPlanDialog(std::function<void()> applied, QWidget* parent) : QDialog(parent) {
    setWindowTitle("Receive Band Plans");
    resize(900, 560);
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    auto* region = new QComboBox;
    auto* country = new QComboBox;
    auto* location = new QComboBox;
    region->setObjectName("bandplan.region");
    country->setObjectName("bandplan.country");
    location->setObjectName("bandplan.location");
    form->addRow("Region", region);
    form->addRow("Country", country);
    form->addRow("Location / profile", location);
    layout->addLayout(form);
    auto* coverage = new QLabel;
    coverage->setWordWrap(true);
    coverage->setTextFormat(Qt::PlainText);
    layout->addWidget(coverage);
    auto* table = new QTableWidget(0, 6);
    table->setHorizontalHeaderLabels({"Service", "Start MHz", "End MHz (exclusive)", "Demod hint", "Decoder hint", "Source"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table);

    auto updateTable = [=] {
        table->setRowCount(0);
        for (const auto& p : BandPlanCatalog::instance().profiles()) if (p->id == location->currentData().toString().toStdString()) {
            coverage->setText(QString::fromStdString(p->coverage + " | " + p->revision));
            for (const auto& e : p->entries) {
                const int r = table->rowCount(); table->insertRow(r);
                const QStringList values = {QString::fromStdString(e.name), QString::number(e.startHz / 1e6, 'f', 6),
                    QString::number(e.endHz / 1e6, 'f', 6), bandPlanModeName(e.mode),
                    QString::fromStdString(e.decoder), QString::fromStdString(e.source)};
                for (int c = 0; c < values.size(); ++c) {
                    auto* item = new QTableWidgetItem(values[c]); item->setToolTip(values[c]); table->setItem(r, c, item);
                }
            }
        }
    };
    auto updateLocations = [=] {
        location->clear();
        for (const auto& p : BandPlanCatalog::instance().profiles())
            if (QString::fromStdString(p->region) == region->currentText() && QString::fromStdString(p->country) == country->currentText())
                location->addItem(QString::fromStdString(p->location), QString::fromStdString(p->id));
        updateTable();
    };
    auto updateCountries = [=] {
        country->clear();
        for (const auto& p : BandPlanCatalog::instance().profiles())
            if (QString::fromStdString(p->region) == region->currentText() && country->findText(QString::fromStdString(p->country)) < 0)
                country->addItem(QString::fromStdString(p->country));
        updateLocations();
    };
    auto refresh = [=](const std::string& id) {
        region->clear();
        for (const auto& p : BandPlanCatalog::instance().profiles())
            if (region->findText(QString::fromStdString(p->region)) < 0) region->addItem(QString::fromStdString(p->region));
        for (const auto& p : BandPlanCatalog::instance().profiles()) if (p->id == id) {
            region->setCurrentText(QString::fromStdString(p->region)); updateCountries();
            country->setCurrentText(QString::fromStdString(p->country)); updateLocations();
            location->setCurrentIndex(location->findData(QString::fromStdString(id)));
        }
        updateTable();
    };
    connect(region, &QComboBox::currentIndexChanged, this, updateCountries);
    connect(country, &QComboBox::currentIndexChanged, this, updateLocations);
    connect(location, &QComboBox::currentIndexChanged, this, updateTable);
    refresh(BandPlanCatalog::instance().active()->id);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close);
    auto* import = buttons->addButton("Import...", QDialogButtonBox::ActionRole);
    auto* exportButton = buttons->addButton("Export...", QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [=] {
        if (BandPlanCatalog::instance().select(location->currentData().toString().toStdString())) {
            BandPlanCatalog::instance().persist();
            if (applied) applied();
        }
    });
    connect(import, &QPushButton::clicked, this, [=] {
        const auto path = QFileDialog::getOpenFileName(this, "Import receive band plan", {}, "Band plan (*.json)");
        if (path.isEmpty()) return;
        try {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly) || file.size() > 262144) throw std::runtime_error("Cannot read file, or file exceeds 256 KiB");
            const auto text = file.readAll().toStdString();
            const auto p = parseBandPlan(text);
            BandPlanCatalog::instance().importProfile(text);
            BandPlanCatalog::instance().persist();
            refresh(p.id);
        } catch (const std::exception& error) {
            QMessageBox::warning(this, "Invalid band plan", QString::fromUtf8(error.what()));
        }
    });
    connect(exportButton, &QPushButton::clicked, this, [=] {
        const auto path = QFileDialog::getSaveFileName(this, "Export receive band plan", {}, "Band plan (*.json)");
        if (path.isEmpty()) return;
        for (const auto& p : BandPlanCatalog::instance().profiles()) if (p->id == location->currentData().toString().toStdString()) {
            const auto text = serializeBandPlan(*p);
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(text.data(), static_cast<qint64>(text.size())) != static_cast<qint64>(text.size()) || !file.commit())
                QMessageBox::warning(this, "Export failed", file.errorString());
        }
    });
}
