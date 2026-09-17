#include "P25Aliases.h"
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QLockFile>
#include <cmath>
#include <set>
#include <stdexcept>

namespace {
constexpr qsizetype maxBytes=1024*1024; // DEC-0100 bounded configuration, not telemetry.
void require(bool ok,const char* error) {if(!ok) throw std::runtime_error(error);}
unsigned number(const QJsonValue& value,unsigned maximum) {
    require(value.isDouble(),"Alias ID must be an integer JSON number");
    const double n=value.toDouble();
    require(std::isfinite(n) && n>=0 && n<=maximum && std::floor(n)==n,"Alias ID is out of range");
    return unsigned(n);
}
QString text(const QJsonValue& value,int limit,bool empty=false) {
    require(value.isString(),"Alias text must be a string");
    const auto s=value.toString().trimmed();
    require((empty || !s.isEmpty()) && s.size()<=limit,"Alias text is empty or too long");
    for(const auto c:s) require(!c.isNull() && !c.isLowSurrogate() && !c.isHighSurrogate() &&
        c.category()!=QChar::Other_Control,"Alias text contains unsupported control characters");
    return s;
}
QJsonObject document(const QByteArray& bytes) {
    require(!bytes.isEmpty() && bytes.size()<=maxBytes,"Alias document must be 1 byte to 1 MiB");
    QJsonParseError error;
    const auto doc=QJsonDocument::fromJson(bytes,&error);
    require(error.error==QJsonParseError::NoError && doc.isObject(),"Invalid alias JSON document");
    return doc.object();
}
P25AliasList parse(const QJsonObject& o,bool stored) {
    require(number(o.value("version"),1)==1,"Unsupported alias version");
    P25AliasList list;
    list.wacn=number(o.value("wacn"),0xfffff);list.systemId=number(o.value("systemId"),0xfff);
    list.name=text(o.value("name"),128);list.source=text(o.value("source"),512);
    list.updated=text(o.value("updated"),10);
    require(QDate::fromString(list.updated,Qt::ISODate).isValid() && list.updated.size()==10,"Expected updated date YYYY-MM-DD");
    require(o.value("talkgroups").isArray(),"Missing talkgroups array");
    const auto entries=o.value("talkgroups").toArray();
    require(entries.size()<=10000,"Too many aliases (maximum 10000)");
    for(const auto entry:entries) {
        require(entry.isObject(),"Invalid talkgroup entry");
        const auto item=entry.toObject();const auto id=number(item.value("id"),65535);
        require(id>0,"Talkgroup zero is not a group alias");
        P25Alias alias{text(item.value("name"),128),text(item.value("group"),128,true),false};
        if(stored) {require(item.value("manual").isBool(),"Invalid stored manual flag");alias.manual=item.value("manual").toBool();}
        require(list.talkgroups.emplace(id,std::move(alias)).second,"Duplicate talkgroup in alias list");
    }
    return list;
}
QJsonObject object(const P25AliasList& list) {
    QJsonArray entries;
    for(const auto& [id,a]:list.talkgroups) entries.append(QJsonObject{{"id",int(id)},{"name",a.name},{"group",a.group},{"manual",a.manual}});
    return {{"version",1},{"wacn",int(list.wacn)},{"systemId",int(list.systemId)},
            {"name",list.name},{"source",list.source},{"updated",list.updated},{"talkgroups",entries}};
}
}
P25AliasList parseP25AliasImport(const QByteArray& bytes) {return parse(document(bytes),false);}
QByteArray exportP25AliasList(const P25AliasList& list) {
    const auto bytes=QJsonDocument(object(list)).toJson();parseP25AliasImport(bytes);return bytes;
}
void mergeP25AliasList(P25AliasLists& lists,P25AliasList incoming) {
    for(auto& current:lists) if(current.wacn==incoming.wacn && current.systemId==incoming.systemId) {
        for(const auto& [id,a]:current.talkgroups) if(a.manual) incoming.talkgroups[id]=a;
        // Validate merged size/text before mutation, including protected entries.
        exportP25AliasList(incoming);current=std::move(incoming);return;
    }
    exportP25AliasList(incoming);lists.push_back(std::move(incoming));
}
QString resolveP25Alias(const P25AliasLists& lists,bool known,unsigned wacn,unsigned systemId,unsigned tg,const QString& manual) {
    if(!manual.trimmed().isEmpty()) return manual;
    if(!known) return {};
    for(const auto& list:lists) if(list.wacn==wacn && list.systemId==systemId) {
        const auto it=list.talkgroups.find(tg);if(it!=list.talkgroups.end()) return it->second.name;
    }
    return {};
}
QString p25AliasesPath() {return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/p25_aliases.json";}
QByteArray readP25AliasFile(const QString& path) {
    if(!QFileInfo::exists(path)) return {};
    QFile file(path);require(file.open(QIODevice::ReadOnly),"Cannot read alias file");
    require(file.size()<=maxBytes,"Alias file exceeds 1 MiB");
    const auto bytes=file.read(maxBytes+1);require(file.error()==QFile::NoError && bytes.size()<=maxBytes,"Cannot read bounded alias file");
    // Missing file is distinct from an empty/corrupt existing database.
    require(!bytes.isEmpty(),"Alias file is empty");return bytes;
}
P25AliasLists loadP25AliasDatabase(const QByteArray& bytes) {
    if(bytes.isEmpty()) return {};
    const auto doc=document(bytes);require(number(doc.value("version"),1)==1 && doc.value("lists").isArray(),"Invalid alias database");
    P25AliasLists result;std::set<std::pair<unsigned,unsigned>> systems;
    for(const auto v:doc.value("lists").toArray()) {
        require(v.isObject(),"Invalid stored alias list");auto list=parse(v.toObject(),true);
        require(systems.emplace(list.wacn,list.systemId).second,"Duplicate alias system");result.push_back(std::move(list));
    }
    return result;
}
QByteArray serializeP25AliasDatabase(const P25AliasLists& lists) {
    QJsonArray all;for(const auto& list:lists) all.append(object(list));
    const auto bytes=QJsonDocument(QJsonObject{{"version",1},{"lists",all}}).toJson();
    loadP25AliasDatabase(bytes);return bytes;
}
void saveP25AliasDatabase(const QString& path,const P25AliasLists& lists,const QByteArray& expected) {
    const auto bytes=serializeP25AliasDatabase(lists);
    require(QDir().mkpath(QFileInfo(path).absolutePath()),"Cannot create alias directory");
    QLockFile lock(path+".lock");require(lock.tryLock(0),"Alias database is being edited by another process");
    require(readP25AliasFile(path)==expected,"Alias database changed externally; reopen the editor");
    QSaveFile file(path);require(file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size() && file.commit(),"Cannot save alias database");
}
