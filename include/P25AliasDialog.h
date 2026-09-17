#pragma once
#include "P25Aliases.h"
#include <QDialog>
class QListWidget;
class QTableWidget;
class QLabel;
class QLineEdit;
class P25AliasDialog final:public QDialog {
public:
    explicit P25AliasDialog(const QString& path,QWidget* parent=nullptr);
    void importList(const QByteArray& bytes);
    void accept() override;
private:
    void refreshLists(int selected);
    void refreshTable();
    QString path_;
    QByteArray original_;
    P25AliasLists lists_;
    QListWidget* systems_;
    QTableWidget* table_;
    QLabel* status_;
    QLineEdit* search_;
};
