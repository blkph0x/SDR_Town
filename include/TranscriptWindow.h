#pragma once

#include "SttEngine.h"
#include "TranscriptHub.h"
#include "TranscriptTypes.h"

#include <QDialog>
#include <QHash>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;

// Secondary window: unified chronological decode / voice-to-text log.
// Source-agnostic — new feeds register on TranscriptHub and appear via filters.
class TranscriptWindow : public QDialog
{
    Q_OBJECT
public:
    explicit TranscriptWindow(TranscriptHub* hub,
                              SttEngine* stt,
                              QWidget* parent = nullptr);

    void refreshFromHub();

private slots:
    void onMessageAppended(const TranscriptMessage& msg);
    void onHubCleared();
    void onSourcesChanged();
    void onSttStatusChanged(SttEngineStatus status, const QString& text);
    void onFilterChanged();
    void onClearClicked();
    void onCopyClicked();
    void onSaveClicked();

private:
    void rebuildFilterCombo();
    bool passesFilter(const TranscriptMessage& msg) const;
    QString formatLine(const TranscriptMessage& msg) const;
    void appendVisibleLine(const TranscriptMessage& msg);
    void rebuildVisibleLog();

    TranscriptHub* m_hub = nullptr;
    SttEngine* m_stt = nullptr;

    QPlainTextEdit* m_log = nullptr;
    QComboBox* m_filterCombo = nullptr;
    QCheckBox* m_autoScroll = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QPushButton* m_copyBtn = nullptr;
    QPushButton* m_saveBtn = nullptr;

    QString m_filterSourceId; // empty = all
    QVector<TranscriptMessage> m_cache;
};
