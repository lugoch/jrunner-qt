#include "full_log_dialog.h"

#include "theme.h"
#include "widgets.h"

#include <QApplication>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>

namespace jrunner {

namespace {

constexpr int kMaxLoadedLines = 200000;
constexpr int kMaxSearchResults = 5000;

/// 时间戳前缀不含级别关键字,故高亮重检测与存储级别一致
QString renderHistoryLine(const LogLine &line)
{
    return line.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
        + QStringLiteral("  ") + line.text;
}

QString renderBatch(const QList<LogLine> &lines)
{
    QString batch;
    batch.reserve(lines.size() * 96);
    for (const LogLine &l : lines) {
        batch += renderHistoryLine(l);
        batch += QLatin1Char('\n');
    }
    return batch;
}

} // namespace

LogLineHighlighter::LogLineHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
}

void LogLineHighlighter::highlightBlock(const QString &text)
{
    const LogLevel level = detectLogLevel(text);
    if (level == LogLevel::Plain)
        return;
    QTextCharFormat fmt;
    fmt.setForeground(theme::logLevelColor(level));
    if (level == LogLevel::Error)
        fmt.setBackground(QColor(Badge::hexWithAlpha(theme::danger(), 0.10)));
    else if (level == LogLevel::Warn)
        fmt.setBackground(QColor(Badge::hexWithAlpha(theme::warning(), 0.08)));
    setFormat(0, text.length(), fmt);
}

FullLogDialog::FullLogDialog(ProcessTask *task, QWidget *parent)
    : QDialog(parent)
    , m_task(task)
    , m_logPath(task ? task->logFilePath() : QString())
{
    setObjectName(QStringLiteral("jrFullLogDialog"));
    setWindowTitle(QStringLiteral("完整日志"));
    setWindowFlag(Qt::Window, true);
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(false);
    resize(860, 620);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto *topRow = new QHBoxLayout();
    topRow->setSpacing(8);
    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("jrFullLogSearch"));
    m_search->setPlaceholderText(QStringLiteral("搜索完整日志(回车执行)"));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::returnPressed, this, [this]() { runSearch(); });
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (text.trimmed().isEmpty() && m_searchMode)
            loadBrowse();
    });
    topRow->addWidget(m_search, 1);

    auto *refreshButton = new QPushButton(QStringLiteral("刷新"), this);
    refreshButton->setAutoDefault(false);
    connect(refreshButton, &QPushButton::clicked, this, [this]() {
        if (m_searchMode)
            runSearch();
        else
            loadBrowse();
    });
    topRow->addWidget(refreshButton);

    m_exportButton = new QPushButton(QStringLiteral("导出…"), this);
    m_exportButton->setAutoDefault(false);
    m_exportButton->setEnabled(!m_logPath.isEmpty());
    connect(m_exportButton, &QPushButton::clicked, this, [this]() { exportToFile(); });
    topRow->addWidget(m_exportButton);

    auto *closeButton = new QPushButton(QStringLiteral("关闭"), this);
    closeButton->setAutoDefault(false);
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    topRow->addWidget(closeButton);
    layout->addLayout(topRow);

    m_bannerLabel = new QLabel(this);
    m_bannerLabel->setWordWrap(true);
    m_bannerLabel->setStyleSheet(QStringLiteral("color: %1;").arg(theme::textTertiary().name()));
    m_bannerLabel->hide();
    layout->addWidget(m_bannerLabel);

    m_view = new QPlainTextEdit(this);
    m_view->setObjectName(QStringLiteral("jrFullLogView"));
    m_view->setReadOnly(true);
    m_view->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_view->setUndoRedoEnabled(false);
    QFont font = m_view->font();
    font.setFamily(theme::monospaceFontFamily());
    m_view->setFont(font);
    m_highlighter = new LogLineHighlighter(m_view->document());
    layout->addWidget(m_view, 1);

    m_infoLabel = new QLabel(this);
    m_infoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_infoLabel->setStyleSheet(QStringLiteral("color: %1;").arg(theme::textTertiary().name()));
    layout->addWidget(m_infoLabel);

    if (m_task) {
        // 任务被替换/销毁时关闭本窗口;槽内不解引用 task
        connect(m_task.data(), &QObject::destroyed, this, &QWidget::close);
        connect(m_task.data(), &ProcessTask::logsCleared, this, [this]() { loadBrowse(); });
    }

    loadBrowse();
}

void FullLogDialog::updateInfo()
{
    const quint64 total = m_task ? m_task->totalLineCount() : 0;
    QString info = QStringLiteral("共 %1 行").arg(total);
    if (!m_logPath.isEmpty())
        info += QStringLiteral("    %1").arg(m_logPath);
    m_infoLabel->setText(info);
}

void FullLogDialog::loadBrowse()
{
    m_searchMode = false;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QList<LogLine> lines;
    quint64 total = 0;
    if (m_task) {
        total = m_task->totalLineCount();
        const quint64 start = total > kMaxLoadedLines ? total - kMaxLoadedLines : 0;
        lines = m_task->readHistory(start, kMaxLoadedLines);
    }
    m_view->setPlainText(renderBatch(lines));
    QApplication::restoreOverrideCursor();

    if (total > static_cast<quint64>(lines.size())) {
        m_bannerLabel->setText(QStringLiteral("日志共 %1 行,已加载最近 %2 行;点击「导出…」获取完整文件。")
                                   .arg(total)
                                   .arg(lines.size()));
        m_bannerLabel->show();
    } else {
        m_bannerLabel->hide();
    }
    updateInfo();
    QScrollBar *bar = m_view->verticalScrollBar();
    bar->setValue(bar->maximum());
}

void FullLogDialog::runSearch()
{
    const QString query = m_search->text().trimmed();
    if (query.isEmpty()) {
        loadBrowse();
        return;
    }
    m_searchMode = true;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QList<LogLine> matches;
    if (m_task)
        matches = m_task->searchHistory(query, 0, kMaxSearchResults);
    m_view->setPlainText(renderBatch(matches));
    QApplication::restoreOverrideCursor();

    if (matches.size() >= kMaxSearchResults)
        m_bannerLabel->setText(QStringLiteral("匹配至少 %1 条,已达显示上限;请细化搜索或导出完整文件。")
                                   .arg(kMaxSearchResults));
    else
        m_bannerLabel->setText(QStringLiteral("匹配 %1 条。").arg(matches.size()));
    m_bannerLabel->show();
    updateInfo();
}

void FullLogDialog::exportToFile()
{
    const QString target = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出完整日志"), QStringLiteral("jrunner-log.log"),
        QStringLiteral("日志文件 (*.log *.txt);;所有文件 (*)"), nullptr,
        QFileDialog::DontUseNativeDialog);
    if (target.isEmpty())
        return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = ProcessTask::exportHistoryToFile(m_logPath, target);
    QApplication::restoreOverrideCursor();
    m_bannerLabel->setText(ok ? QStringLiteral("已导出到 %1").arg(target)
                              : QStringLiteral("导出失败:无法写入 %1").arg(target));
    m_bannerLabel->show();
}

} // namespace jrunner