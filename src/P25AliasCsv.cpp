#include "P25Aliases.h"
#include <QStringDecoder>
#include <QStringList>
#include <stdexcept>

namespace {
[[noreturn]] void fail(qsizetype row,const QString& message) {
    throw std::runtime_error(QString("CSV record %1: %2").arg(row).arg(message).toStdString());
}
QString decode(const QByteArray& bytes) {
    if(bytes.isEmpty() || bytes.size()>1024*1024) throw std::runtime_error("CSV must be 1 byte to 1 MiB");
    auto encoding=QStringConverter::Utf8;
    if(bytes.startsWith(QByteArray::fromHex("fffe"))) encoding=QStringConverter::Utf16LE;
    else if(bytes.startsWith(QByteArray::fromHex("feff"))) encoding=QStringConverter::Utf16BE;
    QStringDecoder decoder(encoding,QStringConverter::Flag::Stateless);
    QString result=decoder(bytes);
    if(decoder.hasError()) throw std::runtime_error("Invalid CSV encoding; save as UTF-8 or BOM-marked UTF-16");
    if(result.startsWith(QChar(0xfeff))) result.remove(0,1);
    if(result.contains(QChar(0))) throw std::runtime_error("CSV contains NUL characters");
    return result;
}
// DEC-0101 / RFC4180. Explicit states avoid splitting quoted commas or newlines.
// allowExtraColumns: RadioReference site exports append unnamed frequency columns.
std::vector<QStringList> records(const QString& input,bool allowExtraColumns) {
    enum class State {Start,Plain,Quoted,Closed};State state=State::Start;
    std::vector<QStringList> rows;QStringList row;QString field;qsizetype number=1;
    auto endField=[&] {row.push_back(field);field.clear();state=State::Start;
        if(row.size()>256) fail(number,"too many columns");};
    auto endRow=[&] {endField();bool blank=true;for(const auto& v:row) if(!v.trimmed().isEmpty()) blank=false;
        if(!blank) rows.push_back(row);row.clear();++number;
        if(rows.size()>10001) fail(number,"more than 10000 aliases");};
    for(qsizetype i=0;i<input.size();++i) {
        const auto c=input[i];
        if(state==State::Quoted) {
            if(c=='"') state=State::Closed;else field+=c;
        } else if(state==State::Closed && c=='"') {field+='"';state=State::Quoted;}
        else if(c==',') endField();
        else if(c=='\r' || c=='\n') {endRow();if(c=='\r' && i+1<input.size() && input[i+1]=='\n') ++i;}
        else if(state==State::Closed) fail(number,"unexpected text after closing quote");
        else if(c=='"') {if(state!=State::Start) fail(number,"quote inside unquoted field");state=State::Quoted;}
        else {field+=c;state=State::Plain;}
        if(field.size()>4096) fail(number,"field exceeds 4096 characters");
    }
    if(state==State::Quoted) fail(number,"unterminated quoted field");
    if(state!=State::Start || !field.isEmpty() || !row.isEmpty()) endRow();
    (void)allowExtraColumns;
    return rows;
}
unsigned id(const QString& text,int base,qsizetype row,unsigned minimum,unsigned maximum,const char* label) {
    const auto s=text.trimmed();
    if(s.isEmpty()) fail(row,QString("empty %1").arg(label));
    for(auto c:s) if(!((c>='0' && c<='9') || (base==16 && ((c>='a' && c<='f') || (c>='A' && c<='F')))))
        fail(row,QString("invalid %1").arg(label));
    bool ok=false;const auto n=s.toUInt(&ok,base);
    if(!ok || n<minimum || n>maximum) fail(row,QString("%1 out of range").arg(label));
    return n;
}
std::map<QString,int> headerMap(const QStringList& header,qsizetype row) {
    std::map<QString,int> columns;
    for(int i=0;i<header.size();++i) {
        const auto name=header[i].trimmed().toLower();
        if(name.isEmpty() || !columns.emplace(name,i).second) fail(row,"empty or duplicate column name");
    }
    return columns;
}
void requireColumns(const std::map<QString,int>& columns,std::initializer_list<const char*> names,qsizetype row) {
    for(const auto* name:names) if(!columns.count(name)) fail(row,QString("missing column %1").arg(name));
}
}
P25AliasCsvKind detectP25AliasCsvKind(const QByteArray& bytes) {
    const auto rows=records(decode(bytes),true);
    if(rows.empty()) throw std::runtime_error("CSV record 1: expected header row");
    const auto columns=headerMap(rows[0],1);
    const bool talkgroups=columns.count("decimal") && (columns.count("alpha tag") || columns.count("description"));
    const bool sites=columns.count("rfss") && columns.count("site dec") && columns.count("description");
    if(talkgroups && sites) throw std::runtime_error("CSV record 1: ambiguous talkgroup and site headers");
    if(talkgroups) return P25AliasCsvKind::Talkgroups;
    if(sites) return P25AliasCsvKind::Sites;
    throw std::runtime_error("CSV record 1: expected RadioReference talkgroup columns Decimal/Alpha Tag "
                             "or site columns RFSS/Site Dec/Description");
}
P25AliasList parseP25AliasCsv(const QByteArray& bytes,P25AliasList destination) {
    const auto preservedSites=destination.sites;
    const auto rows=records(decode(bytes),false);
    if(rows.size()<2) fail(1,"expected header and at least one talkgroup");
    const auto columns=headerMap(rows[0],1);
    if(!columns.count("decimal") || (!columns.count("alpha tag") && !columns.count("description")))
        fail(1,"expected RadioReference talkgroup columns Decimal and Alpha Tag or Description (not a frequency/site export)");
    destination.talkgroups.clear();
    for(size_t i=1;i<rows.size();++i) {
        const auto& row=rows[i];
        if(row.size()!=rows[0].size()) fail(i+1,"column count differs from header");
        auto value=[&](const char* header) {auto col=columns.find(header);return col==columns.end()?QString():row[col->second].trimmed();};
        const auto tg=id(value("decimal"),10,i+1,1,65535,"talkgroup ID");
        if(!value("hex").isEmpty() && id(value("hex"),16,i+1,1,65535,"hex talkgroup ID")!=tg)
            fail(i+1,"Decimal and Hex IDs disagree");
        auto name=value("alpha tag").simplified();if(name.isEmpty()) name=value("description").simplified();
        auto group=value("category").simplified();if(group.isEmpty()) group=value("tag").simplified();
        if(name.isEmpty() || name.size()>128 || group.size()>128) fail(i+1,"empty or overlong alias name/group");
        if(!destination.talkgroups.emplace(tg,P25Alias{name,group,false}).second) fail(i+1,"duplicate talkgroup ID");
        // Mode, encryption suffixes, priority and other columns are deliberately metadata-only/ignored.
    }
    destination.sites=preservedSites;
    exportP25AliasList(destination);return destination;
}
P25AliasList parseP25AliasSitesCsv(const QByteArray& bytes,P25AliasList destination) {
    const auto preservedTalkgroups=destination.talkgroups;
    const auto rows=records(decode(bytes),true);
    if(rows.size()<2) fail(1,"expected header and at least one site");
    const auto columns=headerMap(rows[0],1);
    requireColumns(columns,{"rfss","site dec","description"},1);
    destination.sites.clear();
    const int headerCols=rows[0].size();
    for(size_t i=1;i<rows.size();++i) {
        const auto& row=rows[i];
        // RadioReference site exports append unnamed frequency columns after Frequencies.
        if(int(row.size())<headerCols) fail(i+1,"column count shorter than header");
        auto value=[&](const char* header) {auto col=columns.find(header);return col==columns.end()?QString():row[col->second].trimmed();};
        const auto rfss=id(value("rfss"),10,i+1,0,255,"RFSS");
        const auto site=id(value("site dec"),10,i+1,0,65535,"site ID");
        if(!value("site hex").isEmpty() && id(value("site hex"),16,i+1,0,65535,"hex site ID")!=site)
            fail(i+1,"Site Dec and Site Hex disagree");
        auto name=value("description").simplified();
        auto group=value("county name").simplified();
        if(name.isEmpty() || name.size()>128 || group.size()>128) fail(i+1,"empty or overlong site name/group");
        const P25SiteKey key{rfss,site};
        auto existing=destination.sites.find(key);
        if(existing==destination.sites.end()) {
            destination.sites.emplace(key,P25SiteAlias{name,group,false});
        } else {
            // RadioReference site exports can list multiple names for one RFSS/site.
            if(existing->second.name!=name) {
                const auto merged=(existing->second.name+" / "+name).left(128).trimmed();
                if(merged.isEmpty()) fail(i+1,"empty merged site name");
                existing->second.name=merged;
            }
            if(existing->second.group.isEmpty()) existing->second.group=group;
        }
        // NAC, lat/lon, range and frequency columns are presentation-ignored (never RF policy).
    }
    destination.talkgroups=preservedTalkgroups;
    exportP25AliasList(destination);return destination;
}
