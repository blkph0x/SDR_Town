#include "P25Aliases.h"
#include <catch2/catch_test_macros.hpp>
#include "P25TalkgroupRegistry.h"
#include <QTableWidget>

TEST_CASE("Talkgroup table selection follows identity not refreshed row order", "[aliases][identity]") {
    P25TalkgroupEntry first, second;
    first.controlFreqHz = second.controlFreqHz = 420.350e6;
    first.talkgroupId = 1; second.talkgroupId = 2;
    QTableWidget table(1, 1);
    auto* item = new QTableWidgetItem("1");
    item->setData(Qt::UserRole, p25TalkgroupKey(first));
    table.setItem(0, 0, item);
    table.setCurrentCell(0, 0);
    REQUIRE(selectedP25TalkgroupIndex(&table, {second, first}) == 1);
    REQUIRE(selectedP25TalkgroupIndex(&table, {second}) == -1);
    REQUIRE(selectedP25TalkgroupIndex(nullptr, {first}) == -1);
}

TEST_CASE("Sorted talkgroup refresh preserves selection after edits and clears deletion", "[aliases][identity]") {
    P25TalkgroupEntry first, second;
    first.controlFreqHz = second.controlFreqHz = 420.350e6;
    first.talkgroupId = 100; second.talkgroupId = 200;
    first.alphaTag = "Zulu"; second.alphaTag = "Alpha";
    QTableWidget table(0, 10);
    table.setSortingEnabled(true);
    table.sortItems(2, Qt::AscendingOrder);
    populateP25TalkgroupTable(&table, {first, second});
    REQUIRE(selectP25Talkgroup(&table, first));
    REQUIRE(table.currentRow() == 1);
    REQUIRE(selectedP25TalkgroupIndex(&table, {first, second}) == 0);
    first.alphaTag = "Aardvark";
    first.verified = true;
    populateP25TalkgroupTable(&table, {second, first});
    REQUIRE(table.currentRow() == 0);
    REQUIRE(selectedP25TalkgroupIndex(&table, {second, first}) == 1);
    REQUIRE(selectP25Talkgroup(&table, second));
    REQUIRE(table.currentRow() == 1);
    populateP25TalkgroupTable(&table, {first});
    REQUIRE(selectedP25TalkgroupIndex(&table, {first}) == -1);
    REQUIRE_FALSE(selectP25Talkgroup(&table, second));
    REQUIRE_FALSE(selectP25Talkgroup(nullptr, first));
}

TEST_CASE("Talkgroup metadata cannot cross control source or known system", "[p25][aliases][identity]") {
    P25TalkgroupEntry a, b;
    a.talkgroupId = b.talkgroupId = 30003;
    a.controlFreqHz = b.controlFreqHz = 420.350e6;
    REQUIRE(p25SameMetadataSource(a, b));
    b.controlFreqHz = 420.475e6;
    REQUIRE_FALSE(p25SameMetadataSource(a, b));
    b.controlFreqHz = a.controlFreqHz;
    a.p25MaskParamsKnown = b.p25MaskParamsKnown = true;
    a.systemId = 1; b.systemId = 2;
    REQUIRE_FALSE(p25SameMetadataSource(a, b));
    b.systemId = 1;
    REQUIRE(p25SameMetadataSource(a, b));
    b.nac = 0x123;
    REQUIRE_FALSE(p25SameMetadataSource(a, b));
    b.nac = 0;
    a.siteId = 1; b.siteId = 2;
    REQUIRE_FALSE(p25SameMetadataSource(a, b));
    b.controlFreqHz = 0;
    REQUIRE_FALSE(p25SameMetadataSource(a, b));
}
#include "P25AliasDialog.h"
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDate>
#include <QTableWidget>
#include <QListWidget>
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
    CHECK(formatP25TalkgroupStatusLabel(123,true,781824,1,"Dispatch")=="TG 123 Dispatch");
    CHECK(formatP25TalkgroupStatusLabel(123,false,781824,1,"Local")=="TG 123 Local");
    CHECK(formatP25TalkgroupStatusLabel(0,true,781824,1)=="TG ?");
    // Cached site helper must stay exception-safe on the control-log hot path.
    CHECK(resolveCachedP25SiteAlias(true,781824,1,1,1).isEmpty());
    CHECK(resolveCachedP25SiteAlias(false,781824,1,1,1,"Tower")=="Tower");
    CHECK(resolveP25Alias(lists,false,781824,1,123)=="Dispatch");
    CHECK(resolveP25Alias(lists,true,781824,2,123).isEmpty());
    CHECK(resolveP25Alias(lists,true,781825,1,123).isEmpty());
    CHECK(resolveP25Alias(lists,true,781824,1,124).isEmpty());
    CHECK(resolveP25Alias(lists,false,0,0,123,"My name")=="My name");
    lists[0].talkgroups[123]={"Local override","Local",true};
    list.talkgroups[456]={"New import","Other",false};mergeP25AliasList(lists,list);
    CHECK(lists[0].talkgroups.at(123).name=="Local override");
    CHECK(lists[0].talkgroups.at(456).name=="New import");
    list.systemId=2;mergeP25AliasList(lists,list);CHECK(lists.size()==2);
    // Unknown-system fallback is allowed only when all imported systems agree.
    CHECK(resolveP25Alias(lists,false,0,0,123).isEmpty());
    CHECK(resolveP25Alias(lists,false,0,0,456)=="New import");
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
    // AppData database wrapper must not be parsed as a single-list import document.
    const auto db=serializeP25AliasDatabase(lists);
    CHECK_THROWS(parseP25AliasImport(db));
    CHECK(loadP25AliasDatabase(db).size()==lists.size());
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

TEST_CASE("P25 alias AppData database loads into dialog","[gui][probealias]") {
    const auto path=qEnvironmentVariable("SDR_TOWN_PROBE_ALIAS_PATH");
    if(path.isEmpty()) SKIP("Set SDR_TOWN_PROBE_ALIAS_PATH to the AppData p25_aliases.json");
    const auto bytes=readP25AliasFile(path);
    const auto lists=loadP25AliasDatabase(bytes);
    REQUIRE(lists.size()>=1);
    CHECK(lists[0].talkgroups.size()>=100);
    P25AliasDialog window(path);
    window.show();
    QApplication::processEvents();
    auto* systems=window.findChild<QListWidget*>("aliasSystems");
    auto* table=window.findChild<QTableWidget*>("aliasTable");
    auto* sites=window.findChild<QTableWidget*>("aliasSitesTable");
    auto* status=window.findChild<QLabel*>("aliasStatus");
    REQUIRE(systems);REQUIRE(table);REQUIRE(sites);REQUIRE(status);
    CHECK(systems->count()==int(lists.size()));
    CHECK(systems->currentRow()==0);
    CHECK(table->rowCount()==int(lists[0].talkgroups.size()));
    CHECK(sites->rowCount()==int(lists[0].sites.size()));
    CHECK(status->text().contains(QString::number(lists[0].talkgroups.size())));
    WARN(status->text().toStdString());
}

TEST_CASE("P25 alias CSV imports RadioReference talkgroups and sites","[aliases]") {
    P25AliasList destination;
    destination.wacn=0xbee00;destination.systemId=1;destination.name="NSWGRN example";
    destination.source="test";destination.updated="2026-09-18";
    const QByteArray tgCsv=
        "Decimal,Hex,Alpha Tag,Mode,Description,Tag,Category\n"
        "10000,2710,\"1121 GL 01\",\"De\",\"Government Liaison 01\",\"Interop\",\"Shared Liaisons (GLOs/ESOs)\"\n"
        "10001,2711,\"1122 GL 02\",\"De\",\"Government Liaison 02\",\"Interop\",\"Shared Liaisons (GLOs/ESOs)\"\n";
    CHECK(detectP25AliasCsvKind(tgCsv)==P25AliasCsvKind::Talkgroups);
    auto list=parseP25AliasCsv(tgCsv,destination);
    CHECK(list.talkgroups.size()==2);
    CHECK(list.talkgroups.at(10000).name=="1121 GL 01");
    CHECK(list.talkgroups.at(10000).group=="Shared Liaisons (GLOs/ESOs)");
    CHECK(resolveP25Alias({list},true,0xbee00,1,10000)=="1121 GL 01");
    CHECK(resolveP25Alias({list},false,0xbee00,1,10000)=="1121 GL 01");

    const QByteArray sitesCsv=
        "RFSS,Site Dec,Site Hex,Site NAC,Description,County Name,Lat,Lon,Range,Frequencies\n"
        "1,002,2,2DA,\"Beecroft (Pennant Hills)\",\"Sydney Outer (SO)\",-33.739969,151.058025,25,413.137500,415.137500,416.137500c\n"
        "1,003,3,2DF,\"Bilgola Plateau\",\"Sydney Outer (SO)\",-33.643501,151.314081,25,415.762500\n";
    CHECK(detectP25AliasCsvKind(sitesCsv)==P25AliasCsvKind::Sites);
    auto withSites=parseP25AliasSitesCsv(sitesCsv,list);
    CHECK(withSites.talkgroups.size()==2);
    CHECK(withSites.sites.size()==2);
    CHECK(withSites.sites.at(P25SiteKey{1,2}).name=="Beecroft (Pennant Hills)");
    CHECK(withSites.sites.at(P25SiteKey{1,2}).group=="Sydney Outer (SO)");
    CHECK(resolveP25SiteAlias({withSites},true,0xbee00,1,1,2)=="Beecroft (Pennant Hills)");
    CHECK(resolveP25SiteAlias({withSites},true,0xbee00,1,1,99).isEmpty());

    // Reimport sites preserves talkgroups and manual site overrides via merge.
    P25AliasLists lists{withSites};
    lists[0].sites[P25SiteKey{1,2}].manual=true;
    lists[0].sites[P25SiteKey{1,2}].name="Local Beecroft";
    auto sitesOnly=parseP25AliasSitesCsv(
        "RFSS,Site Dec,Site Hex,Site NAC,Description,County Name,Lat,Lon,Range,Frequencies\n"
        "1,003,3,2DF,\"Bilgola Plateau\",\"Sydney Outer (SO)\",,,,\n",list);
    mergeP25AliasList(lists,sitesOnly);
    CHECK(lists[0].talkgroups.size()==2);
    CHECK(lists[0].sites.size()==2);
    CHECK(lists[0].sites.at(P25SiteKey{1,2}).name=="Local Beecroft");
    CHECK(lists[0].sites.at(P25SiteKey{1,3}).name=="Bilgola Plateau");

    CHECK_THROWS(parseP25AliasCsv(sitesCsv,destination));
    CHECK_THROWS(parseP25AliasSitesCsv(tgCsv,destination));
    CHECK_THROWS(parseP25AliasCsv(
        "Decimal,Hex,Alpha Tag,Mode,Description,Tag,Category\n"
        "10000,2711,\"bad hex\",\"De\",\"x\",\"Interop\",\"g\"\n",destination));
    CHECK_THROWS(parseP25AliasCsv("Frequency,Name\n413.1,cc\n",destination));
    auto badQuote=QByteArray("Decimal,Alpha Tag\n1,\"unterminated\n");
    CHECK_THROWS(parseP25AliasCsv(badQuote,destination));
    // Duplicate RFSS/site rows merge display names (real RR site exports do this).
    auto dupSites=parseP25AliasSitesCsv(
        "RFSS,Site Dec,Site Hex,Description,County Name\n"
        "3,148,94,\"Springbrook\",\"Northern Rivers (NR)\"\n"
        "3,148,94,\"Tweed Heads\",\"Northern Rivers (NR)\"\n",destination);
    CHECK(dupSites.sites.at(P25SiteKey{3,148}).name=="Springbrook / Tweed Heads");
}

TEST_CASE("P25 alias GUI stages CSV into an explicit destination system","[aliases][gui]") {
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
    command("New list...",{"NSWGRN","BEE00","001"});
    const QByteArray tgCsv=
        "Decimal,Hex,Alpha Tag,Mode,Description,Tag,Category\n"
        "10000,2710,\"1121 GL 01\",\"De\",\"Government Liaison 01\",\"Interop\",\"Shared\"\n";
    int index=0;bool matched=true;QStringList answers{
        "NSWGRN — WACN bee00 / System 001"};
    std::function<void()> respond;
    respond=[&] {
        auto* dialog=qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
        if(!dialog || index>=answers.size()) {matched=false;return;}
        dialog->setTextValue(answers[index]);++index;dialog->accept();
        if(index<answers.size()) QTimer::singleShot(0,&window,respond);
    };
    QTimer::singleShot(0,&window,respond);
    REQUIRE(window.importCsv(tgCsv,"trs_tg_6943.csv"));
    REQUIRE(matched);REQUIRE(index==answers.size());
    auto* table=window.findChild<QTableWidget*>("aliasTable");REQUIRE(table);CHECK(table->rowCount()==1);
    CHECK(table->item(0,1)->text()=="1121 GL 01");
    const QByteArray sitesCsv=
        "RFSS,Site Dec,Site Hex,Site NAC,Description,County Name,Lat,Lon,Range,Frequencies\n"
        "1,002,2,2DA,\"Beecroft (Pennant Hills)\",\"Sydney Outer (SO)\",-33.7,151.0,25,413.1,415.1\n";
    index=0;matched=true;
    QTimer::singleShot(0,&window,respond);
    REQUIRE(window.importCsv(sitesCsv,"trs_sites_6943.csv"));
    REQUIRE(matched);
    auto* sites=window.findChild<QTableWidget*>("aliasSitesTable");REQUIRE(sites);CHECK(sites->rowCount()==1);
    CHECK(sites->item(0,2)->text()=="Beecroft (Pennant Hills)");
    CHECK(table->rowCount()==1);
    window.accept();
    const auto lists=loadP25AliasDatabase(readP25AliasFile(path));REQUIRE(lists.size()==1);
    CHECK(lists[0].talkgroups.at(10000).name=="1121 GL 01");
    CHECK(lists[0].sites.at(P25SiteKey{1,2}).name=="Beecroft (Pennant Hills)");
}

TEST_CASE("P25 alias CSV live import helper writes AppData database","[liveimport]") {
    if(qEnvironmentVariable("SDR_TOWN_ALIAS_IMPORT_ENABLE")!="1") {
        SKIP("Set SDR_TOWN_ALIAS_IMPORT_ENABLE=1 plus TG/SITES/OUT paths");
    }
    const auto tgPath=qEnvironmentVariable("SDR_TOWN_ALIAS_IMPORT_TG");
    const auto sitesPath=qEnvironmentVariable("SDR_TOWN_ALIAS_IMPORT_SITES");
    const auto outPath=qEnvironmentVariable("SDR_TOWN_ALIAS_IMPORT_OUT");
    if(tgPath.isEmpty() || sitesPath.isEmpty() || outPath.isEmpty()) {
        SKIP("Set SDR_TOWN_ALIAS_IMPORT_TG/SITES/OUT to run live CSV import");
    }
    bool ok=false;
    const auto wacnText=qEnvironmentVariable("SDR_TOWN_ALIAS_IMPORT_WACN","bee00");
    const auto sysText=qEnvironmentVariable("SDR_TOWN_ALIAS_IMPORT_SYS","2d1");
    const auto name=qEnvironmentVariable("SDR_TOWN_ALIAS_IMPORT_NAME","NSWGRN");
    const auto wacn=wacnText.toUInt(&ok,16);REQUIRE(ok);REQUIRE(wacn<=0xfffff);
    const auto systemId=sysText.toUInt(&ok,16);REQUIRE(ok);REQUIRE(systemId<=0xfff);

    P25AliasList destination;
    destination.wacn=wacn;destination.systemId=systemId;destination.name=name;
    destination.source="CSV live import";destination.updated=QDate::currentDate().toString(Qt::ISODate);

    const auto tgBytes=readP25AliasFile(tgPath);
    const auto siteBytes=readP25AliasFile(sitesPath);
    REQUIRE(detectP25AliasCsvKind(tgBytes)==P25AliasCsvKind::Talkgroups);
    REQUIRE(detectP25AliasCsvKind(siteBytes)==P25AliasCsvKind::Sites);

    auto list=parseP25AliasCsv(tgBytes,destination);
    list=parseP25AliasSitesCsv(siteBytes,list);
    REQUIRE(list.talkgroups.size()>=100);
    REQUIRE(list.sites.size()>=10);
    CHECK(list.talkgroups.count(10000));
    CHECK(list.sites.count(P25SiteKey{1,2}));

    P25AliasLists lists;
    if(QFileInfo::exists(outPath)) {
        const auto existing=readP25AliasFile(outPath);
        if(!existing.isEmpty()) lists=loadP25AliasDatabase(existing);
    }
    mergeP25AliasList(lists,std::move(list));
    const QByteArray expected=QFileInfo::exists(outPath)?readP25AliasFile(outPath):QByteArray{};
    saveP25AliasDatabase(outPath,lists,expected);

    const auto verify=loadP25AliasDatabase(readP25AliasFile(outPath));
    REQUIRE_FALSE(verify.empty());
    const auto* imported=[&]()->const P25AliasList* {
        for(const auto& entry:verify) if(entry.wacn==wacn && entry.systemId==systemId) return &entry;
        return nullptr;
    }();
    REQUIRE(imported!=nullptr);
    CHECK(imported->talkgroups.size()>=100);
    CHECK(imported->sites.size()>=10);
    CHECK(resolveP25Alias(verify,true,wacn,systemId,10000)==imported->talkgroups.at(10000).name);
    CHECK(resolveP25SiteAlias(verify,true,wacn,systemId,1,2)==imported->sites.at(P25SiteKey{1,2}).name);
    WARN("Imported "+std::to_string(imported->talkgroups.size())+" talkgroups and "+
         std::to_string(imported->sites.size())+" sites into "+outPath.toStdString());
}
