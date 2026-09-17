#include "P25Aliases.h"
#include "P25AliasDialog.h"
#include <catch2/catch_test_macros.hpp>
#include <QTemporaryDir>
#include <QFile>
#include <QTableWidget>
#include <QLabel>
#include <QApplication>
#include <QLineEdit>
#include <QLockFile>
#include <QInputDialog>
#include <QPushButton>
#include <QTimer>
#include <functional>

namespace { QByteArray sample=R"({"version":1,"wacn":781824,"systemId":1,"name":"Example system","source":"Fictional test data","updated":"2026-09-18","talkgroups":[{"id":123,"name":"Dispatch","group":"Example"}]})"; }
TEST_CASE("P25 alias lists strictly validate imported identifiers and preserve manual names","[aliases]") {
    auto list=parseP25AliasImport(sample);P25AliasLists lists;
    mergeP25AliasList(lists,list);
    CHECK(resolveP25Alias(lists,true,781824,1,123)=="Dispatch");
    CHECK(resolveP25Alias(lists,false,781824,1,123).isEmpty());
    CHECK(resolveP25Alias(lists,true,781824,2,123).isEmpty());
    CHECK(resolveP25Alias(lists,true,781825,1,123).isEmpty());
    CHECK(resolveP25Alias(lists,true,781824,1,124).isEmpty());
    CHECK(resolveP25Alias(lists,false,0,0,123,"My name")=="My name");
    lists[0].talkgroups[123]={"Local override","Local",true};
    list.talkgroups[456]={"New import","Other",false};mergeP25AliasList(lists,list);
    CHECK(lists[0].talkgroups.at(123).name=="Local override");
    CHECK(lists[0].talkgroups.at(456).name=="New import");
    list.systemId=2;mergeP25AliasList(lists,list);CHECK(lists.size()==2);
    const auto bytes=serializeP25AliasDatabase(lists);
    CHECK(serializeP25AliasDatabase(loadP25AliasDatabase(bytes))==bytes);
    CHECK_FALSE(parseP25AliasImport(exportP25AliasList(lists[0])).talkgroups.at(123).manual);
    for(const auto& bad:std::vector<QByteArray>{"", "[]", "{broken",QByteArray(1024*1024+1,' ')}) CHECK_THROWS(parseP25AliasImport(bad));
    for(const auto& replacement:std::vector<QByteArray>{"-1","1.5","65536","\"123\"","null","0"}) {
        auto bad=sample;bad.replace("\"id\":123","\"id\":"+replacement);CHECK_THROWS(parseP25AliasImport(bad));
    }
    auto duplicate=sample;duplicate.replace("}]}","},{\"id\":123,\"name\":\"Duplicate\",\"group\":\"\"}]}");CHECK_THROWS(parseP25AliasImport(duplicate));
    auto badDate=sample;badDate.replace("2026-09-18","2026-02-30");CHECK_THROWS(parseP25AliasImport(badDate));
    auto badWacn=sample;badWacn.replace("781824","1048576");CHECK_THROWS(parseP25AliasImport(badWacn));
    auto badSystem=sample;badSystem.replace("\"systemId\":1","\"systemId\":4096");CHECK_THROWS(parseP25AliasImport(badSystem));
    auto emptyName=sample;emptyName.replace("Dispatch","");CHECK_THROWS(parseP25AliasImport(emptyName));
    auto badText=sample;badText.replace("Dispatch","Line\\nBreak");CHECK_THROWS(parseP25AliasImport(badText));
    auto unicode=sample;unicode.replace("Dispatch","Caf\\u00e9");CHECK(parseP25AliasImport(unicode).talkgroups.at(123).name==QString::fromUtf8("Caf\xc3\xa9"));
    auto absent=list;absent.talkgroups.clear();mergeP25AliasList(lists,absent);
    CHECK(lists[1].talkgroups.empty()); // Omitted imported rows are removed, not stale aliases.
    auto duplicates=lists;duplicates.push_back(lists[0]);CHECK_THROWS(serializeP25AliasDatabase(duplicates));
}
TEST_CASE("P25 alias persistence is atomic and refuses concurrent or corrupt replacement","[aliases]") {
    QTemporaryDir dir;REQUIRE(dir.isValid());const auto path=dir.filePath("aliases.json");
    P25AliasLists lists{parseP25AliasImport(sample)};saveP25AliasDatabase(path,lists,{});
    const auto original=readP25AliasFile(path);REQUIRE(!original.isEmpty());
    lists[0].name="Changed";CHECK_THROWS(saveP25AliasDatabase(path,lists,{}));CHECK(readP25AliasFile(path)==original);
    saveP25AliasDatabase(path,lists,original);CHECK(loadP25AliasDatabase(readP25AliasFile(path))[0].name=="Changed");
    QLockFile lock(path+".lock");REQUIRE(lock.tryLock(0));
    CHECK_THROWS(saveP25AliasDatabase(path,lists,readP25AliasFile(path)));lock.unlock();
    QFile file(dir.filePath("empty.json"));REQUIRE(file.open(QIODevice::WriteOnly));file.close();CHECK_THROWS(readP25AliasFile(file.fileName()));
    CHECK_THROWS(loadP25AliasDatabase("not json"));
}
TEST_CASE("P25 alias GUI imports with review cancel and save plus conflict reporting","[aliases][gui]") {
    QTemporaryDir dir;REQUIRE(dir.isValid());const auto path=dir.filePath("aliases.json");
    {P25AliasDialog window(path);window.importList(sample);window.reject();CHECK_FALSE(QFile::exists(path));}
    P25AliasDialog window(path);window.show();window.importList(sample);QApplication::processEvents();
    CHECK(window.findChild<QLabel*>("aliasStatus")->textFormat()==Qt::PlainText);
    auto* table=window.findChild<QTableWidget*>("aliasTable");REQUIRE(table);CHECK(table->rowCount()==1);CHECK(table->item(0,1)->text()=="Dispatch");
    window.findChild<QLineEdit*>("aliasSearch")->setText("absent");CHECK(table->rowCount()==0);
    window.findChild<QLineEdit*>("aliasSearch")->clear();CHECK(table->rowCount()==1);
    CHECK_THROWS(window.importList("broken"));CHECK(table->rowCount()==1);
    window.resize(560,380);QApplication::processEvents();
    const auto screenshot=qEnvironmentVariable("SDR_TOWN_ALIAS_SCREENSHOT");if(!screenshot.isEmpty()) REQUIRE(window.grab().save(screenshot));
    window.accept();CHECK(window.result()==QDialog::Accepted);CHECK(loadP25AliasDatabase(readP25AliasFile(path)).size()==1);
    P25AliasDialog conflict(path);const auto original=readP25AliasFile(path);auto changed=loadP25AliasDatabase(original);changed[0].name="Another process";
    saveP25AliasDatabase(path,changed,original);conflict.accept();CHECK(conflict.result()!=QDialog::Accepted);
    CHECK(conflict.findChild<QLabel*>("aliasStatus")->text().contains("changed externally"));
}

TEST_CASE("P25 alias GUI creates and edits protected local entries","[aliases][gui]") {
    QTemporaryDir dir;REQUIRE(dir.isValid());const auto path=dir.filePath("aliases.json");
    P25AliasDialog window(path);window.show();
    auto command=[&](const QString& label,const QStringList& answers) {
        QPushButton* selected=nullptr;for(auto* b:window.findChildren<QPushButton*>()) if(b->text()==label) selected=b;
        REQUIRE(selected);int index=0;bool matched=true;
        std::function<void()> respond;
        respond=[&] {
            auto* dialog=qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
            if(!dialog || index>=answers.size()) {matched=false;return;}
            if(dialog->inputMode()==QInputDialog::IntInput) dialog->setIntValue(answers[index].toInt());
            else dialog->setTextValue(answers[index]);
            ++index;dialog->accept();
            if(index<answers.size()) QTimer::singleShot(0,&window,respond);
        };
        QTimer::singleShot(0,&window,respond);selected->click();
        REQUIRE(matched);REQUIRE(index==answers.size());
    };
    command("New list...",{"My system","BEE00","001"});
    command("Add alias...",{"123","Local dispatch"});
    auto* table=window.findChild<QTableWidget*>("aliasTable");REQUIRE(table->rowCount()==1);
    table->selectRow(0);command("Edit alias...",{"My Dispatch","Operations"});
    window.importList(sample);CHECK(table->item(0,1)->text()=="My Dispatch");CHECK(table->item(0,3)->text()=="Manual");
    window.accept();const auto lists=loadP25AliasDatabase(readP25AliasFile(path));REQUIRE(lists.size()==1);
    CHECK(lists[0].talkgroups.at(123).name=="My Dispatch");CHECK(lists[0].talkgroups.at(123).group=="Operations");
}
