#include "log_console.h"

#include "full_log_dialog.h"
#include "theme.h"
#include "widgets.h"

#include <QClipboard>
#include <QComboBox>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>

namespace jrunner {

namespace {

QString htmlEscape(const QString &s)
{
    QString out = s.toHtmlEscaped();
    return out;
}

QString levelMarker(LogLevel level)
{
    switch (level) {
    case LogLevel::Error: return QStringLiteral("!");
    case LogLevel::Warn: return QStringLiteral("▲");
    case LogLevel::Debug: return QStringLiteral("·");
    default: return QString();
    }
}

/// 日志行 HTML:时间戳 + 级别标记 + 内容,error/warn 行带浅底色
QString lineHtml(const LogLine &line)
{
    const QColor levelColor = theme::logLevelColor(line.level);
    const QColor textColor = line.level == LogLevel::Plain ? theme::textPrimary() : levelColor;
    const QString time = line.timestamp.toString(QStringLiteral("HH:mm:ss.zzz"));

    QString rowBg;
    if (line.level == LogLevel::Error)
        rowBg = Badge::hexWithAlpha(theme::danger(), 0.10);
    else if (line.level == LogLevel::Warn)
        rowBg = Badge::hexWithAlpha(theme::warning(), 0.08);

    QString html = QStringLiteral("<span style='%1'>%2</span> ")
                       .arg(rowBg.isEmpty() ? QString()
                                            : QStringLiteral("background-color:%1;").arg(rowBg),
                            time);
    const QString marker = levelMarker(line.level);
    if (!marker.isEmpty()) {
        html += QStringLiteral("<span style='color:%1;font-weight:bold;'>%2</span> ")
                    .arg(levelColor.name(), marker);
    } else {
        html += QStringLiteral("&nbsp;");
    }
    html += QStringLiteral("<span style='color:%1;'>%2</span>")
                .arg(textColor.name(), htmlEscape(line.text));
    return html;
}

} // namespace

LogConsole::LogConsole(AppStore *store, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
{
    setObjectName(QStringLiteral("jrRightPanel"));
    setMinimumWidth(360);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ---- 工具栏 ----
    auto *toolbar = new QWidget(this);
    auto *toolbarLayout = new QVBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(12, 7, 12, 7);
    toolbarLayout->setSpacing(6);

    auto *row1 = new QHBoxLayout();
    row1->setSpacing(8);
    auto *terminalIcon = new QLabel(QStringLiteral("❯"), toolbar);
    terminalIcon->setStyleSheet(
        QStringLiteral("color: %1; font-size: 12px;").arg(theme::textTertiary().name()));
    m_titleLabel = new QLabel(QStringLiteral("控制台"), toolbar);
    m_titleLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 12px; font-weight: 600;")
            .arg(theme::textSecondary().name()));
    m_statusDot = new StatusDot(toolbar);
    row1->addWidget(terminalIcon);
    row1->addWidget(m_titleLabel, 1);
    row1->addWidget(m_statusDot);
    row1->addSpacing(8);

    m_levelFilter = new QComboBox(toolbar);
    m_levelFilter->setToolTip(QStringLiteral("级别筛选"));
    m_levelFilter->addItems({ QStringLiteral("全部"), QStringLiteral("仅错误"),
                              QStringLiteral("错误 + 警告"), QStringLiteral("含调试") });
    m_levelFilter->setCurrentIndex(3);      // 默认含调试(即不过滤)
    connect(m_levelFilter, &QComboBox::currentIndexChanged, this, [this]() { rebuildLog(); });
    row1->addWidget(m_levelFilter);

    m_autoScrollButton = makeIconButton(QStringLiteral("go-bottom"), QStyle::SP_ArrowDown,
                                        QStringLiteral("自动滚动"), toolbar);
    m_autoScrollButton->setProperty("role", "flat");
    m_autoScrollButton->setCheckable(true);
    m_autoScrollButton->setChecked(m_autoScroll);
    m_autoScrollButton->setToolTip(m_autoScroll ? QStringLiteral("自动滚动已开启")
                                               : QStringLiteral("自动滚动已暂停"));
    connect(m_autoScrollButton, &QPushButton::toggled, this, [this](bool checked) {
        m_autoScroll = checked;
        m_autoScrollButton->setToolTip(m_autoScroll ? QStringLiteral("自动滚动已开启")
                                                    : QStringLiteral("自动滚动已暂停"));
    });
    row1->addWidget(m_autoScrollButton);

    m_clearButton = makeIconButton(QStringLiteral("user-trash"), QStyle::SP_TrashIcon,
                                   QStringLiteral("清空日志"), toolbar);
    m_clearButton->setProperty("role", "flat");
    connect(m_clearButton, &QPushButton::clicked, this, [this]() {
        if (m_task)
            m_task->clearLogs();
    });
    row1->addWidget(m_clearButton);

    m_fullLogButton = makeIconButton(QStringLiteral("document-open"), QStyle::SP_FileDialogDetailedView,
                                     QStringLiteral("完整日志"), toolbar);
    m_fullLogButton->setObjectName(QStringLiteral("jrFullLogButton"));
    m_fullLogButton->setProperty("role", "flat");
    m_fullLogButton->setEnabled(false);
    connect(m_fullLogButton, &QPushButton::clicked, this, [this]() {
        if (!m_task)
            return;
        if (m_fullLogDialog) {
            m_fullLogDialog->raise();
            m_fullLogDialog->activateWindow();
            return;
        }
        m_fullLogDialog = new FullLogDialog(m_task, this);
        m_fullLogDialog->show();
        m_fullLogDialog->raise();
    });
    row1->addWidget(m_fullLogButton);
    toolbarLayout->addLayout(row1);

    auto *row2 = new QHBoxLayout();
    row2->setSpacing(8);
    m_filterEdit = new QLineEdit(toolbar);
    m_filterEdit->setPlaceholderText(QStringLiteral("过滤日志"));
    m_filterEdit->setFixedWidth(140);
    m_filterDebounce = new QTimer(this);
    m_filterDebounce->setSingleShot(true);
    m_filterDebounce->setInterval(300);
    connect(m_filterDebounce, &QTimer::timeout, this, [this]() { rebuildLog(); });
    connect(m_filterEdit, &QLineEdit::textChanged, this, [this]() { m_filterDebounce->start(); });
    m_subtitleLabel = new QLabel(toolbar);
    m_subtitleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_subtitleLabel->setTextFormat(Qt::PlainText);
    QFont subFont = m_subtitleLabel->font();
    subFont.setFamily(theme::monospaceFontFamily());
    subFont.setPointSize(8);
    m_subtitleLabel->setFont(subFont);
    m_subtitleLabel->setStyleSheet(
        QStringLiteral("color: %1;").arg(theme::textTertiary().name()));
    row2->addWidget(m_filterEdit);
    row2->addWidget(m_subtitleLabel, 1);
    toolbarLayout->addLayout(row2);

    layout->addWidget(toolbar);

    auto *divider = new QFrame(this);
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet(
        QStringLiteral("color: %1;").arg(theme::border().name(QColor::HexArgb)));
    layout->addWidget(divider);

    // ---- 错误横幅 ----
    m_errorBanner = new QWidget(this);
    m_errorBanner->setStyleSheet(
        QStringLiteral("QWidget { background: %1; }")
            .arg(Badge::hexWithAlpha(theme::danger(), 0.08)));
    auto *bannerLayout = new QHBoxLayout(m_errorBanner);
    bannerLayout->setContentsMargins(12, 7, 12, 7);
    bannerLayout->setSpacing(7);
    auto *bannerIcon = new QLabel(QStringLiteral("⚠"), m_errorBanner);
    bannerIcon->setStyleSheet(
        QStringLiteral("color: %1; font-size: 11px;").arg(theme::danger().name()));
    m_errorLabel = new QLabel(m_errorBanner);
    m_errorLabel->setWordWrap(true);
    QFont errFont = m_errorLabel->font();
    errFont.setFamily(theme::monospaceFontFamily());
    errFont.setPointSize(9);
    m_errorLabel->setFont(errFont);
    m_errorLabel->setStyleSheet(
        QStringLiteral("color: %1;").arg(theme::danger().name()));
    m_errorLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *copyButton = new QPushButton(QStringLiteral("复制"), m_errorBanner);
    copyButton->setProperty("role", "danger");
    connect(copyButton, &QPushButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(m_errorLabel->text());
    });
    bannerLayout->addWidget(bannerIcon);
    bannerLayout->addWidget(m_errorLabel, 1);
    bannerLayout->addWidget(copyButton);
    m_errorBanner->hide();
    layout->addWidget(m_errorBanner);

    // ---- 日志视图 ----
    m_logView = new QPlainTextEdit(this);
    m_logView->setObjectName(QStringLiteral("jrLogView"));
    m_logView->setReadOnly(true);
    m_logView->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_logView->setUndoRedoEnabled(false);
    m_logView->setMaximumBlockCount(20000);
    m_logView->setContextMenuPolicy(Qt::NoContextMenu);
    layout->addWidget(m_logView, 1);

    // ---- 状态联动 ----
    connect(m_store, &AppStore::activeTaskChanged, this, [this]() {
        ProcessTask *task = m_store->activeTask();
        setTask(task);
    });
    connect(m_store, &AppStore::selectionChanged, this, [this]() {
        // 切换模块时同步到该模块自己的任务(没有任务就清空)
        const auto project = m_store->selectedProject();
        const auto module = m_store->selectedModule();
        if (project && module)
            setTask(m_store->task(project->path, module->path));
        else
            setTask(nullptr);
    });
    connect(m_store, &AppStore::taskStateChanged, this, [this](const QString &) {
        refreshToolbar();
        refreshErrorBanner();
        // 环形缓冲回绕或任务切换都会触发全量重建,交给 linesAppended 增量路径
    });

    // 初始任务:当前选中模块
    const auto project = m_store->selectedProject();
    const auto module = m_store->selectedModule();
    if (project && module)
        setTask(m_store->task(project->path, module->path));
}

void LogConsole::setTask(ProcessTask *task)
{
    if (m_task == task)
        return;
    if (m_fullLogDialog)
        m_fullLogDialog->close();
    if (m_task) {
        disconnect(m_task, nullptr, this, nullptr);
    }
    m_task = task;
    m_renderedCount = 0;
    m_cachedErrorSummary.clear();
    if (m_task) {
        connect(m_task, &ProcessTask::linesAppended, this, [this](int firstIndex, int count) {
            // 无过滤时增量追加;有过滤或环形回绕时全量重建
            const bool incremental = firstIndex == m_renderedCount
                && m_levelFilter->currentIndex() != 1 && m_levelFilter->currentIndex() != 2
                && m_filterEdit->text().trimmed().isEmpty();
            if (incremental) {
                appendLines(firstIndex, count);
            } else {
                rebuildLog();
            }
        });
        connect(m_task, &ProcessTask::logsCleared, this, [this]() { rebuildLog(); });
        connect(m_task, &ProcessTask::stateChanged, this, [this]() {
            refreshToolbar();
            refreshErrorBanner();
        });
    }
    m_logView->clear();
    rebuildLog();
    refreshToolbar();
    refreshErrorBanner();
}

void LogConsole::refreshTheme()
{
    refreshToolbar();
    refreshErrorBanner();
    rebuildLog();
}

void LogConsole::refreshToolbar()
{
    if (m_task) {
        m_titleLabel->setText(m_task->title());
        m_titleLabel->setStyleSheet(
            QStringLiteral("color: %1; font-size: 12px; font-weight: 600;")
                .arg(theme::textPrimary().name()));
        m_statusDot->setTaskState(m_task->state());
        m_subtitleLabel->setToolTip(m_task->subtitle());
        m_subtitleLabel->setText(m_subtitleLabel->fontMetrics().elidedText(
            m_task->subtitle(), Qt::ElideMiddle, qMax(0, m_subtitleLabel->width())));
        m_fullLogButton->setEnabled(true);
        m_fullLogButton->setToolTip(QStringLiteral("完整日志\n%1\n共 %2 行")
                                        .arg(m_task->logFilePath())
                                        .arg(m_task->totalLineCount()));
    } else {
        m_titleLabel->setText(QStringLiteral("控制台"));
        m_titleLabel->setStyleSheet(
            QStringLiteral("color: %1; font-size: 12px; font-weight: 600;")
                .arg(theme::textSecondary().name()));
        m_statusDot->setTaskState(TaskState{});
        m_subtitleLabel->clear();
        m_fullLogButton->setEnabled(false);
        m_fullLogButton->setToolTip(QStringLiteral("完整日志"));
    }
}

void LogConsole::refreshErrorBanner()
{
    if (!m_task || m_task->state().kind != TaskState::Failed) {
        m_errorBanner->hide();
        m_cachedErrorSummary.clear();
        return;
    }
    if (m_cachedErrorSummary.isEmpty())
        m_cachedErrorSummary = errorSummary(m_task);
    if (m_cachedErrorSummary.isEmpty()) {
        m_errorBanner->hide();
        return;
    }
    m_errorLabel->setText(m_cachedErrorSummary);
    m_errorBanner->show();
}

QString LogConsole::errorSummary(const ProcessTask *task)
{
    if (!task || task->state().kind != TaskState::Failed)
        return QString();
    QStringList texts;
    for (const LogLine &l : task->lines())
        texts.append(l.text.trimmed());

    // 最具体的异常链优先
    int idx = -1;
    for (int i = texts.size() - 1; i >= 0; --i) {
        if (texts[i].startsWith(QStringLiteral("Caused by:"))) {
            idx = i;
            break;
        }
    }
    if (idx >= 0)
        return texts[idx].replace(QStringLiteral("Caused by: "), QString());

    for (int i = texts.size() - 1; i >= 0; --i) {
        if (texts[i].contains(QStringLiteral("APPLICATION FAILED TO START"))) {
            if (i + 2 < texts.size())
                return texts[i + 2];
            return texts[i];
        }
    }
    for (int i = texts.size() - 1; i >= 0; --i) {
        if (texts[i].contains(QStringLiteral("What went wrong"))) {
            if (i + 1 < texts.size())
                return texts[i + 1];
            break;
        }
    }
    for (int i = texts.size() - 1; i >= 0; --i) {
        if (!texts[i].isEmpty() && !texts[i].startsWith(QStringLiteral("BUILD FAILED")))
            return texts[i];
    }
    return QString();
}

QList<LogLine> LogConsole::visibleLines() const
{
    if (!m_task)
        return {};
    QList<LogLine> result = m_task->lines();
    // 级别过滤
    switch (m_levelFilter->currentIndex()) {
    case 1:
        result.erase(std::remove_if(result.begin(), result.end(),
                                    [](const LogLine &l) { return l.level != LogLevel::Error; }),
                     result.end());
        break;
    case 2:
        result.erase(std::remove_if(result.begin(), result.end(),
                                    [](const LogLine &l) {
                                        return l.level != LogLevel::Error && l.level != LogLevel::Warn;
                                    }),
                     result.end());
        break;
    default:
        break;
    }
    // 文本过滤
    const QString q = m_filterEdit->text().trimmed().toLower();
    if (!q.isEmpty()) {
        result.erase(std::remove_if(result.begin(), result.end(),
                                    [&](const LogLine &l) {
                                        return !l.text.toLower().contains(q);
                                    }),
                     result.end());
    }
    return result;
}

void LogConsole::rebuildLog()
{
    m_logView->clear();
    m_renderedCount = 0;
    if (!m_task)
        return;

    const QList<LogLine> lines = visibleLines();
    QString batch;
    batch.reserve(lines.size() * 128);
    for (const LogLine &l : lines) {
        batch += lineHtml(l);
        batch += QLatin1Char('\n');
    }
    if (!batch.isEmpty())
        m_logView->appendHtml(batch);
    m_renderedCount = m_task->lines().size();

    if (m_autoScroll) {
        QScrollBar *bar = m_logView->verticalScrollBar();
        bar->setValue(bar->maximum());
    }
}

void LogConsole::appendLines(int firstIndex, int count)
{
    // 无过滤时的增量追加;QPlainTextEdit 的 maximumBlockCount 负责环形裁剪
    const QList<LogLine> &all = m_task->lines();
    QString batch;
    const int end = qMin(all.size(), firstIndex + count);
    batch.reserve((end - firstIndex) * 128);
    for (int i = firstIndex; i < end; ++i) {
        batch += lineHtml(all[i]);
        batch += QLatin1Char('\n');
    }
    if (!batch.isEmpty())
        m_logView->appendHtml(batch);
    m_renderedCount = end;

    if (m_autoScroll) {
        QScrollBar *bar = m_logView->verticalScrollBar();
        bar->setValue(bar->maximum());
    }
}

} // namespace jrunner
