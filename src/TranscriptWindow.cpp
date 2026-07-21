#include "TranscriptWindow.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QFile>
#include <QTextCursor>
#include <QTextStream>

TranscriptWindow::TranscriptWindow(TranscriptHub* hub, SttEngine* stt, QWidget* parent)
    : QDialog(parent)
    , m_hub(hub)
    , m_stt(stt)
{
    setWindowTitle(QStringLiteral("Decode Log / Transcript"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(980, 560);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    auto* top = new QHBoxLayout();
    top->addWidget(new QLabel(QStringLiteral("Filter:"), this));
    m_filterCombo = new QComboBox(this);
    m_filterCombo->setMinimumWidth(180);
    top->addWidget(m_filterCombo);

    m_autoScroll = new QCheckBox(QStringLiteral("Auto-scroll"), this);
    m_autoScroll->setChecked(true);
    top->addWidget(m_autoScroll);

    top->addStretch(1);

    m_statusLabel = new QLabel(QStringLiteral("STT: —"), this);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #9ad; padding: 2px 8px;"));
    top->addWidget(m_statusLabel);
    root->addLayout(top);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_log->setMaximumBlockCount(4000);
    QFont mono(QStringLiteral("Consolas"));
    if (!mono.exactMatch()) mono = QFont(QStringLiteral("Courier New"));
    mono.setPointSize(10);
    m_log->setFont(mono);
    root->addWidget(m_log, 1);

    auto* btns = new QHBoxLayout();
    m_clearBtn = new QPushButton(QStringLiteral("Clear"), this);
    m_copyBtn = new QPushButton(QStringLiteral("Copy"), this);
    m_saveBtn = new QPushButton(QStringLiteral("Save…"), this);
    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this);
    btns->addWidget(m_clearBtn);
    btns->addWidget(m_copyBtn);
    btns->addWidget(m_saveBtn);
    btns->addStretch(1);
    btns->addWidget(closeBtn);
    root->addLayout(btns);

    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);
    connect(m_clearBtn, &QPushButton::clicked, this, &TranscriptWindow::onClearClicked);
    connect(m_copyBtn, &QPushButton::clicked, this, &TranscriptWindow::onCopyClicked);
    connect(m_saveBtn, &QPushButton::clicked, this, &TranscriptWindow::onSaveClicked);
    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TranscriptWindow::onFilterChanged);

    if (m_hub) {
        connect(m_hub, &TranscriptHub::messageAppended,
                this, &TranscriptWindow::onMessageAppended);
        connect(m_hub, &TranscriptHub::cleared,
                this, &TranscriptWindow::onHubCleared);
        connect(m_hub, &TranscriptHub::sourcesChanged,
                this, &TranscriptWindow::onSourcesChanged);
        m_cache = m_hub->messages();
    }
    if (m_stt) {
        connect(m_stt, &SttEngine::statusChanged,
                this, &TranscriptWindow::onSttStatusChanged);
        onSttStatusChanged(m_stt->status(), m_stt->statusText());
    }

    rebuildFilterCombo();
    rebuildVisibleLog();
}

void TranscriptWindow::refreshFromHub()
{
    if (!m_hub) return;
    m_cache = m_hub->messages();
    rebuildFilterCombo();
    rebuildVisibleLog();
}

void TranscriptWindow::rebuildFilterCombo()
{
    if (!m_filterCombo) return;
    const QString previous = m_filterSourceId;
    m_filterCombo->blockSignals(true);
    m_filterCombo->clear();
    m_filterCombo->addItem(QStringLiteral("All sources"), QString());

    QVector<TranscriptSourceInfo> sources;
    if (m_hub) sources = m_hub->sources();
    for (const auto& src : sources) {
        const QString label = src.enabled
            ? QStringLiteral("%1 (%2)").arg(src.displayName, transcriptCategoryDisplayName(src.category))
            : QStringLiteral("%1 (reserved)").arg(src.displayName);
        m_filterCombo->addItem(label, src.id);
    }

    int idx = 0;
    for (int i = 0; i < m_filterCombo->count(); ++i) {
        if (m_filterCombo->itemData(i).toString() == previous) {
            idx = i;
            break;
        }
    }
    m_filterCombo->setCurrentIndex(idx);
    m_filterSourceId = m_filterCombo->currentData().toString();
    m_filterCombo->blockSignals(false);
}

bool TranscriptWindow::passesFilter(const TranscriptMessage& msg) const
{
    if (m_filterSourceId.isEmpty()) return true;
    return msg.sourceId == m_filterSourceId;
}

QString TranscriptWindow::formatLine(const TranscriptMessage& msg) const
{
    const QString ts = msg.timestamp.toString(QStringLiteral("HH:mm:ss.zzz"));
    QString channel = msg.channel.trimmed();
    if (!channel.isEmpty()) channel = QStringLiteral(" | %1").arg(channel);
    return QStringLiteral("[%1] [%2]%3  %4")
        .arg(ts)
        .arg(msg.sourceName)
        .arg(channel)
        .arg(msg.text);
}

void TranscriptWindow::appendVisibleLine(const TranscriptMessage& msg)
{
    if (!m_log || !passesFilter(msg)) return;
    m_log->appendPlainText(formatLine(msg));
    if (m_autoScroll && m_autoScroll->isChecked()) {
        auto cursor = m_log->textCursor();
        cursor.movePosition(QTextCursor::End);
        m_log->setTextCursor(cursor);
    }
}

void TranscriptWindow::rebuildVisibleLog()
{
    if (!m_log) return;
    m_log->clear();
    QStringList lines;
    lines.reserve(m_cache.size());
    for (const auto& msg : m_cache) {
        if (passesFilter(msg)) lines << formatLine(msg);
    }
    m_log->setPlainText(lines.join(QLatin1Char('\n')));
    if (m_autoScroll && m_autoScroll->isChecked()) {
        auto cursor = m_log->textCursor();
        cursor.movePosition(QTextCursor::End);
        m_log->setTextCursor(cursor);
    }
}

void TranscriptWindow::onMessageAppended(const TranscriptMessage& msg)
{
    m_cache.push_back(msg);
    while (m_cache.size() > TranscriptHub::kMaxMessages) {
        m_cache.removeFirst();
    }
    appendVisibleLine(msg);
}

void TranscriptWindow::onHubCleared()
{
    m_cache.clear();
    if (m_log) m_log->clear();
}

void TranscriptWindow::onSourcesChanged()
{
    rebuildFilterCombo();
}

void TranscriptWindow::onSttStatusChanged(SttEngineStatus status, const QString& text)
{
    if (!m_statusLabel) return;
    QString color = QStringLiteral("#9ad");
    switch (status) {
    case SttEngineStatus::Ready:         color = QStringLiteral("#8d8"); break;
    case SttEngineStatus::Listening:     color = QStringLiteral("#6cf"); break;
    case SttEngineStatus::Transcribing:  color = QStringLiteral("#fc6"); break;
    case SttEngineStatus::Error:         color = QStringLiteral("#f88"); break;
    case SttEngineStatus::Unavailable:   color = QStringLiteral("#fa6"); break;
    }
    m_statusLabel->setStyleSheet(
        QStringLiteral("color: %1; padding: 2px 8px;").arg(color));
    m_statusLabel->setText(text);
    m_statusLabel->setToolTip(text);
}

void TranscriptWindow::onFilterChanged()
{
    if (!m_filterCombo) return;
    m_filterSourceId = m_filterCombo->currentData().toString();
    rebuildVisibleLog();
}

void TranscriptWindow::onClearClicked()
{
    if (m_hub) m_hub->clear();
    else onHubCleared();
}

void TranscriptWindow::onCopyClicked()
{
    if (!m_log) return;
    if (auto* clip = QGuiApplication::clipboard()) {
        clip->setText(m_log->toPlainText());
    }
}

void TranscriptWindow::onSaveClicked()
{
    if (!m_log) return;
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Save Decode Log"),
        QStringLiteral("decode_log.txt"),
        QStringLiteral("Text files (*.txt);;All files (*.*)"));
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("Save failed"),
                             QStringLiteral("Could not write:\n%1").arg(path));
        return;
    }
    QTextStream out(&f);
    out << m_log->toPlainText();
    f.close();
}
