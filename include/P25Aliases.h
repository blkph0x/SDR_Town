#pragma once
#include <QByteArray>
#include <QString>
#include <map>
#include <vector>

// DEC-0100: labels only. Never feed imported fields into RF/security decisions.
struct P25Alias { QString name, group; bool manual=false; };
struct P25AliasList {
    unsigned wacn=0, systemId=0;
    QString name, source, updated;
    std::map<unsigned,P25Alias> talkgroups;
};
using P25AliasLists=std::vector<P25AliasList>;
P25AliasList parseP25AliasImport(const QByteArray& bytes);
QByteArray exportP25AliasList(const P25AliasList& list);
void mergeP25AliasList(P25AliasLists& lists,P25AliasList incoming);
QString resolveP25Alias(const P25AliasLists& lists,bool systemKnown,unsigned wacn,
                        unsigned systemId,unsigned talkgroup,const QString& manual={});
QString p25AliasesPath();
QByteArray readP25AliasFile(const QString& path);
P25AliasLists loadP25AliasDatabase(const QByteArray& bytes);
QByteArray serializeP25AliasDatabase(const P25AliasLists& lists);
void saveP25AliasDatabase(const QString& path,const P25AliasLists& lists,const QByteArray& expected);
