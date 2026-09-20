#pragma once

#include <QDialog>
#include <vector>
#include <string>

class QListWidget;

class SatCatalogueDialog : public QDialog {
    Q_OBJECT
public:
    explicit SatCatalogueDialog(QWidget* parent = nullptr);

    std::vector<std::string> selectedIds() const;

private:
    QListWidget* list_ = nullptr;
};
