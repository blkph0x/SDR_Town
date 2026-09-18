#pragma once
#include <QByteArray>
#include <QString>
#include <map>
#include <utility>
#include <vector>

// DEC-0100/0101/0102: labels only. Never feed imported fields into RF/security decisions.
struct P25Alias { QString name, group; bool manual=false; };
struct P25SiteAlias { QString name, group; bool manual=false; };
using P25SiteKey = std::pair<unsigned,unsigned>; // rfss, siteId
struct P25AliasList {
    unsigned wacn=0, systemId=0;
    QString name, source, updated;
    std::map<unsigned,P25Alias> talkgroups;
    std::map<P25SiteKey,P25SiteAlias> sites;
};
using P25AliasLists=std::vector<P25AliasList>;
P25AliasList parseP25AliasImport(const QByteArray& bytes);
P25AliasList parseP25AliasCsv(const QByteArray& bytes,P25AliasList destination);
P25AliasList parseP25AliasSitesCsv(const QByteArray& bytes,P25AliasList destination);
enum class P25AliasCsvKind { Talkgroups, Sites };
P25AliasCsvKind detectP25AliasCsvKind(const QByteArray& bytes);
QByteArray exportP25AliasList(const P25AliasList& list);
void mergeP25AliasList(P25AliasLists& lists,P25AliasList incoming);
QString resolveP25Alias(const P25AliasLists& lists,bool systemKnown,unsigned wacn,
                        unsigned systemId,unsigned talkgroup,const QString& manual={});
QString resolveP25SiteAlias(const P25AliasLists& lists,bool systemKnown,unsigned wacn,
                            unsigned systemId,unsigned rfss,unsigned siteId,const QString& manual={});
// Presentation-only status text: "TG 10120" or "TG 10120 Dispatch".
QString formatP25TalkgroupStatusLabel(unsigned talkgroupId,bool systemKnown,unsigned wacn,
                                      unsigned systemId,const QString& manual={});
// Hot-path safe: uses the process alias cache (no per-call disk reparse).
QString resolveCachedP25SiteAlias(bool systemKnown,unsigned wacn,unsigned systemId,
                                  unsigned rfss,unsigned siteId,const QString& manual={});
void invalidateP25AliasCache();
QString p25AliasesPath();
QByteArray readP25AliasFile(const QString& path);
P25AliasLists loadP25AliasDatabase(const QByteArray& bytes);
QByteArray serializeP25AliasDatabase(const P25AliasLists& lists);
void saveP25AliasDatabase(const QString& path,const P25AliasLists& lists,const QByteArray& expected);
