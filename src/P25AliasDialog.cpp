#include "P25AliasDialog.h"
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QDate>
#include <QGridLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <stdexcept>

P25AliasDialog::P25AliasDialog(const QString& path,QWidget* parent):QDialog(parent),path_(path) {
    QString loadError;
    try {
        original_=readP25AliasFile(path_);
        lists_=loadP25AliasDatabase(original_);
    } catch(const std::exception& e) {
        original_={};lists_={};loadError=QString::fromUtf8(e.what());
    }
    setWindowTitle(QString("P25 Alias Lists — %1").arg(QDir::toNativeSeparators(path_)));
    resize(860,560);setMinimumSize(560,380);
    auto* layout=new QVBoxLayout(this);
    auto* actions=new QGridLayout;int actionIndex=0;
    auto button=[&](const char* name,QStyle::StandardPixmap icon) {
        auto* b=new QPushButton(style()->standardIcon(icon),name,this);actions->addWidget(b,actionIndex/3,actionIndex%3);++actionIndex;return b;
    };
    auto* create=button("New list...",QStyle::SP_FileIcon);
    auto* import=button("Import JSON...",QStyle::SP_DialogOpenButton);
    auto* importCsvButton=button("Import CSV...",QStyle::SP_FileDialogContentsView);
    auto* exportButton=button("Export...",QStyle::SP_DialogSaveButton);
    auto* add=button("Add alias...",QStyle::SP_FileIcon);
    auto* edit=button("Edit alias...",QStyle::SP_FileDialogDetailedView);
    auto* remove=button("Remove list",QStyle::SP_TrashIcon);
    layout->addLayout(actions);
    search_=new QLineEdit(this);search_->setPlaceholderText("Search ID, name or group");search_->setObjectName("aliasSearch");layout->addWidget(search_);
    auto* split=new QSplitter(this);systems_=new QListWidget(split);systems_->setObjectName("aliasSystems");
    systems_->setMinimumWidth(180);
    auto* tabs=new QTabWidget(split);tabs->setObjectName("aliasTabs");
    table_=new QTableWidget(0,4);table_->setObjectName("aliasTable");
    table_->setHorizontalHeaderLabels({"TGID","Name","Group","Origin"});table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    sitesTable_=new QTableWidget(0,5);sitesTable_->setObjectName("aliasSitesTable");
    sitesTable_->setHorizontalHeaderLabels({"RFSS","Site","Name","Group","Origin"});
    sitesTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    sitesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);sitesTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    sitesTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    sitesTable_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
    tabs->addTab(table_,"Talkgroups");tabs->addTab(sitesTable_,"Sites");
    split->setStretchFactor(0,0);split->setStretchFactor(1,1);split->setSizes({240,620});layout->addWidget(split);
    status_=new QLabel(this);status_->setObjectName("aliasStatus");status_->setTextFormat(Qt::PlainText);status_->setWordWrap(true);layout->addWidget(status_);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,this);layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,this,&P25AliasDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    connect(systems_,&QListWidget::currentRowChanged,this,[this]{refreshTable();});
    connect(search_,&QLineEdit::textChanged,this,[this]{refreshTable();});
    connect(create,&QPushButton::clicked,this,[this] {
        bool ok=false;P25AliasList list;
        list.name=QInputDialog::getText(this,"New alias list","List name:",QLineEdit::Normal,{},&ok).trimmed();if(!ok) return;
        const auto wacn=QInputDialog::getText(this,"P25 system","WACN (hex, 00000-FFFFF):",QLineEdit::Normal,{},&ok);if(!ok) return;
        list.wacn=wacn.toUInt(&ok,16);if(!ok || list.wacn>0xfffff) {status_->setText("Invalid WACN");return;}
        const auto system=QInputDialog::getText(this,"P25 system","System ID (hex, 000-FFF):",QLineEdit::Normal,{},&ok);if(!ok) return;
        list.systemId=system.toUInt(&ok,16);if(!ok || list.systemId>0xfff) {status_->setText("Invalid System ID");return;}
        for(const auto& existing:lists_) if(existing.wacn==list.wacn && existing.systemId==list.systemId) {status_->setText("This system already has a list");return;}
        list.source="Manual";list.updated=QDate::currentDate().toString(Qt::ISODate);
        try {importList(exportP25AliasList(list));}catch(const std::exception& e){status_->setText(QString::fromUtf8(e.what()));}
    });
    connect(add,&QPushButton::clicked,this,[this] {
        const int index=systems_->currentRow();if(index<0) return;bool ok=false;
        const auto id=unsigned(QInputDialog::getInt(this,"Add alias","Talkgroup ID (decimal):",1,1,65535,1,&ok));if(!ok) return;
        if(lists_[index].talkgroups.count(id)) {status_->setText("Talkgroup already exists; use Edit alias");return;}
        const auto name=QInputDialog::getText(this,"Add alias","Name:",QLineEdit::Normal,{},&ok).trimmed();if(!ok) return;
        auto candidate=lists_;candidate[index].talkgroups[id]={name,{},true};
        try {serializeP25AliasDatabase(candidate);lists_=std::move(candidate);refreshTable();status_->setText("Manual alias staged; Save to apply");}
        catch(const std::exception& e){status_->setText(QString::fromUtf8(e.what()));}
    });
    connect(import,&QPushButton::clicked,this,[this] {
        const auto file=QFileDialog::getOpenFileName(this,"Import P25 alias JSON",{},"Alias JSON (*.json)");if(file.isEmpty()) return;
        try {importList(readP25AliasFile(file));}catch(const std::exception& e){status_->setText(QString::fromUtf8(e.what()));}
    });
    connect(importCsvButton,&QPushButton::clicked,this,[this] {
        const auto file=QFileDialog::getOpenFileName(this,"Import RadioReference CSV",{},
            "RadioReference CSV (*.csv);;All files (*.*)");if(file.isEmpty()) return;
        try {
            if(!importCsv(readP25AliasFile(file),QFileInfo(file).fileName()))
                status_->setText("CSV import cancelled");
        }catch(const std::exception& e){status_->setText(QString::fromUtf8(e.what()));}
    });
    connect(exportButton,&QPushButton::clicked,this,[this] {
        const int row=systems_->currentRow();if(row<0) return;
        const auto path=QFileDialog::getSaveFileName(this,"Export P25 alias list",{},"Alias JSON (*.json)");if(path.isEmpty()) return;
        if(QFileInfo(path).absoluteFilePath().compare(QFileInfo(path_).absoluteFilePath(),Qt::CaseInsensitive)==0 ||
            (!QFileInfo(path).canonicalFilePath().isEmpty() && QFileInfo(path).canonicalFilePath()==QFileInfo(path_).canonicalFilePath())) {
            status_->setText("Export to a different file, not the active alias database");return;
        }
        try {const auto bytes=exportP25AliasList(lists_.at(row));QSaveFile file(path);
            if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) throw std::runtime_error("Cannot export alias list");
            status_->setText("Alias list exported");
        }catch(const std::exception& e){status_->setText(QString::fromUtf8(e.what()));}
    });
    connect(edit,&QPushButton::clicked,this,[this] {
        const int list=systems_->currentRow();if(list<0) return;bool ok=false;
        auto* tabs=findChild<QTabWidget*>("aliasTabs");
        const bool editSites=tabs && tabs->currentIndex()==1;
        if(!editSites) {
            const int row=table_->currentRow();if(row<0) {status_->setText("Select a talkgroup alias to edit");return;}
            const auto id=table_->item(row,0)->data(Qt::UserRole).toUInt();auto alias=lists_.at(list).talkgroups.at(id);
            alias.name=QInputDialog::getText(this,"Alias name","Name:",QLineEdit::Normal,alias.name,&ok).trimmed();if(!ok) return;
            alias.group=QInputDialog::getText(this,"Alias group","Group:",QLineEdit::Normal,alias.group,&ok).trimmed();if(!ok) return;
            auto candidate=lists_;alias.manual=true;candidate.at(list).talkgroups[id]=alias;
            try {serializeP25AliasDatabase(candidate);lists_=std::move(candidate);refreshTable();status_->setText("Manual override staged; Save to apply");}
            catch(const std::exception& e){status_->setText(QString::fromUtf8(e.what()));}
            return;
        }
        const int row=sitesTable_->currentRow();if(row<0) {status_->setText("Select a site alias to edit");return;}
        const auto rfss=sitesTable_->item(row,0)->data(Qt::UserRole).toUInt();
        const auto site=sitesTable_->item(row,1)->data(Qt::UserRole).toUInt();
        auto alias=lists_.at(list).sites.at(P25SiteKey{rfss,site});
        alias.name=QInputDialog::getText(this,"Site name","Name:",QLineEdit::Normal,alias.name,&ok).trimmed();if(!ok) return;
        alias.group=QInputDialog::getText(this,"Site group","Group:",QLineEdit::Normal,alias.group,&ok).trimmed();if(!ok) return;
        auto candidate=lists_;alias.manual=true;candidate.at(list).sites[P25SiteKey{rfss,site}]=alias;
        try {serializeP25AliasDatabase(candidate);lists_=std::move(candidate);refreshTable();status_->setText("Manual site override staged; Save to apply");}
        catch(const std::exception& e){status_->setText(QString::fromUtf8(e.what()));}
    });
    connect(remove,&QPushButton::clicked,this,[this] {
        const int row=systems_->currentRow();if(row<0) return;
        if(QMessageBox::question(this,"Remove alias list","Remove this list? Existing talkgroup Alpha Tags are unaffected.")!=QMessageBox::Yes) return;
        lists_.erase(lists_.begin()+row);refreshLists(0);status_->setText("Removal staged; Save to apply");
    });
    refreshLists(0);
    if(!loadError.isEmpty()) {
        status_->setText(QString("Failed to load %1\n%2").arg(QDir::toNativeSeparators(path_),loadError));
    } else if(lists_.empty()) {
        status_->setText(QString("No alias lists in %1\nUse New list... or Import CSV...").arg(QDir::toNativeSeparators(path_)));
    }
}
void P25AliasDialog::stageList(P25AliasList list) {
    const auto wacn=list.wacn,system=list.systemId;
    auto candidate=lists_;mergeP25AliasList(candidate,std::move(list));serializeP25AliasDatabase(candidate);
    lists_=std::move(candidate);int selected=0;
    for(int i=0;i<int(lists_.size());++i) if(lists_[i].wacn==wacn && lists_[i].systemId==system) selected=i;
    refreshLists(selected);status_->setText("Import staged; manual overrides preserved. Review and Save to apply.");
}
void P25AliasDialog::importList(const QByteArray& bytes) {
    // AppData p25_aliases.json is a database wrapper {version,lists:[...]}.
    // Accept it on Import JSON so finding that file does not look like a bad ID.
    QJsonParseError error;
    const auto doc=QJsonDocument::fromJson(bytes,&error);
    if(error.error==QJsonParseError::NoError && doc.isObject() &&
       doc.object().value("lists").isArray() && !doc.object().value("talkgroups").isArray()) {
        auto incoming=loadP25AliasDatabase(bytes);
        if(incoming.empty()) throw std::runtime_error("Alias database has no lists");
        auto candidate=lists_;
        for(auto& list:incoming) mergeP25AliasList(candidate,std::move(list));
        serializeP25AliasDatabase(candidate);
        lists_=std::move(candidate);
        refreshLists(0);
        status_->setText(QString("Loaded %1 system list(s) from database file; review and Save.")
            .arg(lists_.size()));
        return;
    }
    stageList(parseP25AliasImport(bytes));
}
bool P25AliasDialog::pickCsvDestination(P25AliasList& destination) {
    QStringList choices;choices << "Create new system...";
    for(const auto& list:lists_)
        choices << QString("%1 — WACN %2 / System %3")
            .arg(list.name).arg(list.wacn,5,16,QChar('0')).arg(list.systemId,3,16,QChar('0'));
    bool ok=false;
    const auto picked=QInputDialog::getItem(this,"CSV destination system",
        "RadioReference CSVs have no WACN/System ID.\nChoose or create the destination system:",
        choices,lists_.empty()?0:1,false,&ok);
    if(!ok) return false;
    if(picked==choices.front()) {
        destination={};
        destination.name=QInputDialog::getText(this,"New alias list","List name:",QLineEdit::Normal,{},&ok).trimmed();if(!ok) return false;
        const auto wacn=QInputDialog::getText(this,"P25 system","WACN (hex, 00000-FFFFF):",QLineEdit::Normal,{},&ok);if(!ok) return false;
        destination.wacn=wacn.toUInt(&ok,16);if(!ok || destination.wacn>0xfffff) {status_->setText("Invalid WACN");return false;}
        const auto system=QInputDialog::getText(this,"P25 system","System ID (hex, 000-FFF):",QLineEdit::Normal,{},&ok);if(!ok) return false;
        destination.systemId=system.toUInt(&ok,16);if(!ok || destination.systemId>0xfff) {status_->setText("Invalid System ID");return false;}
        for(const auto& existing:lists_) if(existing.wacn==destination.wacn && existing.systemId==destination.systemId) {
            status_->setText("This system already has a list; select it as the destination");return false;
        }
        return true;
    }
    const int index=choices.indexOf(picked)-1;
    if(index<0 || index>=int(lists_.size())) return false;
    destination=lists_[index];
    return true;
}
bool P25AliasDialog::importCsv(const QByteArray& bytes,const QString& filename) {
    P25AliasList destination;
    if(!pickCsvDestination(destination)) return false;
    destination.source=QString("CSV: %1").arg(filename).left(512);
    destination.updated=QDate::currentDate().toString(Qt::ISODate);
    const auto kind=detectP25AliasCsvKind(bytes);
    if(kind==P25AliasCsvKind::Talkgroups) stageList(parseP25AliasCsv(bytes,destination));
    else stageList(parseP25AliasSitesCsv(bytes,destination));
    return true;
}
void P25AliasDialog::refreshLists(int selected) {
    systems_->clear();
    for(const auto& list:lists_)
        systems_->addItem(QString("%1  (%2/%3)")
            .arg(list.name)
            .arg(list.wacn,5,16,QChar('0'))
            .arg(list.systemId,3,16,QChar('0')));
    systems_->setCurrentRow(lists_.empty()?-1:std::min(selected,int(lists_.size())-1));
    refreshTable();
}
void P25AliasDialog::refreshTable() {
    table_->setUpdatesEnabled(false);sitesTable_->setUpdatesEnabled(false);
    table_->setRowCount(0);sitesTable_->setRowCount(0);
    const int index=systems_->currentRow();
    if(index<0 || index>=int(lists_.size())) {
        table_->setUpdatesEnabled(true);sitesTable_->setUpdatesEnabled(true);
        if(lists_.empty()) {
            status_->setText(QString("No alias lists in %1").arg(QDir::toNativeSeparators(path_)));
        }
        return;
    }
    const auto& list=lists_[index];const auto query=search_->text().trimmed();
    table_->setRowCount(int(list.talkgroups.size()));
    int tgRow=0;
    for(const auto& [id,a]:list.talkgroups) {
        const auto idText=QString::number(id);
        if(!query.isEmpty() && !idText.contains(query) && !a.name.contains(query,Qt::CaseInsensitive) && !a.group.contains(query,Qt::CaseInsensitive)) continue;
        auto* item=new QTableWidgetItem(idText);item->setData(Qt::UserRole,id);table_->setItem(tgRow,0,item);
        table_->setItem(tgRow,1,new QTableWidgetItem(a.name));table_->setItem(tgRow,2,new QTableWidgetItem(a.group));
        table_->setItem(tgRow,3,new QTableWidgetItem(a.manual?"Manual":"Imported"));
        ++tgRow;
    }
    table_->setRowCount(tgRow);
    sitesTable_->setRowCount(int(list.sites.size()));
    int siteRow=0;
    for(const auto& [key,a]:list.sites) {
        const auto rfssText=QString::number(key.first),siteText=QString::number(key.second);
        if(!query.isEmpty() && !rfssText.contains(query) && !siteText.contains(query) &&
           !a.name.contains(query,Qt::CaseInsensitive) && !a.group.contains(query,Qt::CaseInsensitive)) continue;
        auto* rfssItem=new QTableWidgetItem(rfssText);rfssItem->setData(Qt::UserRole,key.first);sitesTable_->setItem(siteRow,0,rfssItem);
        auto* siteItem=new QTableWidgetItem(siteText);siteItem->setData(Qt::UserRole,key.second);sitesTable_->setItem(siteRow,1,siteItem);
        sitesTable_->setItem(siteRow,2,new QTableWidgetItem(a.name));sitesTable_->setItem(siteRow,3,new QTableWidgetItem(a.group));
        sitesTable_->setItem(siteRow,4,new QTableWidgetItem(a.manual?"Manual":"Imported"));
        ++siteRow;
    }
    sitesTable_->setRowCount(siteRow);
    table_->setUpdatesEnabled(true);sitesTable_->setUpdatesEnabled(true);
    status_->setText(QString("%1 talkgroups, %2 sites | %3 | %4\n%5")
        .arg(list.talkgroups.size()).arg(list.sites.size()).arg(list.source,list.updated)
        .arg(QDir::toNativeSeparators(path_)));
}
void P25AliasDialog::accept() {
    try {saveP25AliasDatabase(path_,lists_,original_);QDialog::accept();}
    catch(const std::exception& e){status_->setText(QString::fromUtf8(e.what()));}
}
