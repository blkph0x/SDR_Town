#include "P25TalkgroupRegistry.h"
#include "P25Aliases.h"

#include <QDateTime>
#include <QSignalBlocker>
#include <QTableWidgetItem>
#include <exception>

// DEC-0116: presentation is testable without the live radio/runtime globals.
QString p25VoiceProtocolShort(P25VoiceProtocol protocol)
{
    switch (protocol) {
        case P25VoiceProtocol::Phase1FDMA: return "P1";
        case P25VoiceProtocol::Phase2TDMA: return "P2";
        case P25VoiceProtocol::Unknown:
        default: return "-";
    }
}
bool p25TalkgroupIsPhase2(const P25TalkgroupEntry& tg)
{
    return tg.voiceProtocol == P25VoiceProtocol::Phase2TDMA || tg.phase2Candidate || tg.tdmaSlotKnown;
}

static QString p25TimeText(qint64 ms)
{
    if (ms <= 0) return "-";
    return QDateTime::fromMSecsSinceEpoch(ms).toString("yyyy-MM-dd HH:mm:ss");
}

QString p25HexId(uint32_t value, int width)
{
    return QString("0x%1").arg(value, width, 16, QLatin1Char('0')).toUpper();
}

void populateP25TalkgroupTable(QTableWidget* table, const std::vector<P25TalkgroupEntry>& talkgroups)
{
    if (!table) return;
    const QSignalBlocker blockSignals(table);
    const auto* selected = table->item(table->currentRow(), 0);
    const QString selectedKey = selected ? selected->data(Qt::UserRole).toString() : QString();
    const bool sorting = table->isSortingEnabled();
    table->setSortingEnabled(false);
    P25AliasLists aliases;
    QString aliasError;
    try { aliases=snapshotP25AliasDatabase(); }
    catch(const std::exception& e) { aliasError=QString::fromUtf8(e.what()); }
    table->setRowCount(static_cast<int>(talkgroups.size()));
    for (int row = 0; row < static_cast<int>(talkgroups.size()); ++row) {
        const auto& tg = talkgroups[static_cast<size_t>(row)];
        QString status = tg.scannerEnabled ? "Scanner"
                       : tg.verified ? "Verified"
                       : "Discovered";
        QString protocol = p25TalkgroupIsPhase2(tg) ? "P2" : p25VoiceProtocolShort(tg.voiceProtocol);
        if (tg.tdmaSlotKnown) protocol += QString(" S%1").arg(static_cast<int>(tg.tdmaSlot));
        if (p25TalkgroupIsPhase2(tg) && tg.p25MaskParamsKnown) protocol += " Meta";
        if (protocol != "-") status = protocol + " / " + status;
        table->setItem(row, 0, new QTableWidgetItem(QString::number(tg.controlFreqHz / 1e6, 'f', 5)));
        table->item(row, 0)->setData(Qt::UserRole, p25TalkgroupKey(tg));
        table->setItem(row, 1, new QTableWidgetItem(QString::number(tg.talkgroupId)));
        // Presentation only: aliases never mutate grant, scanner or encryption fields.
        auto* aliasItem=new QTableWidgetItem(resolveP25Alias(aliases,tg.p25MaskParamsKnown,
            tg.wacn,tg.systemId,tg.talkgroupId,QString::fromStdString(tg.alphaTag)));
        QString tip = aliasError.isEmpty()
            ? QString("WACN %1 / System %2; manual Alpha Tag takes precedence")
                .arg(tg.wacn,5,16,QChar('0')).arg(tg.systemId,3,16,QChar('0'))
            : "Alias database error: "+aliasError;
        if (aliasError.isEmpty() && tg.siteId != 0) {
            const auto siteName = resolveP25SiteAlias(
                aliases, tg.p25MaskParamsKnown, tg.wacn, tg.systemId, tg.rfssId, tg.siteId);
            tip += siteName.isEmpty()
                ? QString("\nSite %1 / RFSS %2").arg(tg.siteId).arg(tg.rfssId)
                : QString("\nSite %1 / RFSS %2: %3").arg(tg.siteId).arg(tg.rfssId).arg(siteName);
        }
        aliasItem->setToolTip(tip);
        table->setItem(row, 2, aliasItem);
        table->setItem(row, 3, new QTableWidgetItem(tg.lastVoiceFreqHz > 0.0 ? QString::number(tg.lastVoiceFreqHz / 1e6, 'f', 5) : "-"));
        table->setItem(row, 4, new QTableWidgetItem(tg.lastSourceId ? p25HexId(tg.lastSourceId, 6) : "-"));
        table->setItem(row, 5, new QTableWidgetItem(QString::number(tg.hitCount)));
        table->setItem(row, 6, new QTableWidgetItem(QString::number(tg.userPriority)));
        table->setItem(row, 7, new QTableWidgetItem(tg.encryptionKnown ? (tg.encrypted ? "Yes" : "No") : "Unknown"));
        table->setItem(row, 8, new QTableWidgetItem(status));
        table->setItem(row, 9, new QTableWidgetItem(p25TimeText(tg.lastSeenMs)));
    }
    table->setSortingEnabled(sorting);
    table->clearSelection();
    table->setCurrentItem(nullptr);
    for (int row = 0; row < table->rowCount(); ++row) {
        if (!selectedKey.isEmpty() && table->item(row, 0)->data(Qt::UserRole).toString() == selectedKey) {
            table->setCurrentCell(row, 0);
            table->selectRow(row);
            break;
        }
    }
}
