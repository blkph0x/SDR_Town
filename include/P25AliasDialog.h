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
    bool importCsv(const QByteArray& bytes,const QString& filename);
    void accept() override;
private:
    void refreshLists(int selected);
    void refreshTable();
    void stageList(P25AliasList list);
    bool pickCsvDestination(P25AliasList& destination);
    QString path_;
    QByteArray original_;
    P25AliasLists lists_;
    QListWidget* systems_;
    QTableWidget* table_;
    QTableWidget* sitesTable_;
    QLabel* status_;
    QLineEdit* search_;
};
