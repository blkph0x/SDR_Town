#include "SatCatalogueDialog.h"
#include "SatPassPlanner.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>

namespace {

QString roleBadges(const SatCatalogueEntry& e) {
    bool voice = false, data = false, sstv = false, apt = false;
    for (const auto& d : e.downlinks) {
        if (d.role == "voice") voice = true;
        if (d.role == "aprs" || d.role == "data") data = true;
        if (d.role == "sstv") sstv = true;
        if (d.role == "apt") apt = true;
    }
    QStringList b;
    if (voice) b << "Voice";
    if (data) b << "Data";
    if (sstv) b << "SSTV";
    if (apt) b << "APT";
    bool anyArm = false;
    for (const auto& d : e.downlinks)
        if (d.armable) anyArm = true;
    if (!anyArm) b << "bookmark-only";
    return b.isEmpty() ? QString() : (" [" + b.join("/") + "]");
}

} // namespace

SatCatalogueDialog::SatCatalogueDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Select satellites");
    setMinimumSize(520, 420);
    auto* root = new QVBoxLayout(this);
    root->addWidget(new QLabel(
        "Choose satellites for pass predictions and Auto-track.\n"
        "Badges: Voice / Data / SSTV / APT. Bookmark-only entries cannot arm a decoder yet."));
    list_ = new QListWidget(this);
    list_->setSelectionMode(QAbstractItemView::NoSelection);
    const auto cat = SatPassPlanner::instance().catalogue();
    for (const auto& e : cat.entries()) {
        const QString text = QString("%1  (NORAD %2)%3")
                                 .arg(QString::fromStdString(e.name))
                                 .arg(e.noradId)
                                 .arg(roleBadges(e));
        auto* item = new QListWidgetItem(text, list_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(e.selected ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, QString::fromStdString(e.id));
        if (!e.notes.empty())
            item->setToolTip(QString::fromStdString(e.notes));
    }
    root->addWidget(list_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

std::vector<std::string> SatCatalogueDialog::selectedIds() const {
    std::vector<std::string> ids;
    for (int i = 0; i < list_->count(); ++i) {
        auto* item = list_->item(i);
        if (item->checkState() == Qt::Checked)
            ids.push_back(item->data(Qt::UserRole).toString().toStdString());
    }
    return ids;
}
