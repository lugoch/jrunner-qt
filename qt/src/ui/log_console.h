#pragma once

#include <QWidget>

#include "app/app_store.h"
#include "core/process_task.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class QVBoxLayout;

namespace jrunner {

// 【日志控制台】对应 macOS 版 LogConsole.swift:任务标题/状态、级别与文本过滤、
// 自动滚动、错误摘要横幅、彩色逐行日志。

class FullLogDialog;

class LogConsole : public QWidget {
    Q_OBJECT

public:
    explicit LogConsole(AppStore *store, QWidget *parent = nullptr);
    void refreshTheme();

    /// 从日志里提炼失败根因,避免用户翻几百行找原因
    static QString errorSummary(const ProcessTask *task);

private:
    void setTask(ProcessTask *task);
    void refreshToolbar();
    void refreshErrorBanner();
    void rebuildLog();
    void appendLines(int firstIndex, int count);
    QString renderLine(const LogLine &line) const;
    QList<LogLine> visibleLines() const;

    AppStore *m_store;
    QPointer<ProcessTask> m_task;
    QPointer<FullLogDialog> m_fullLogDialog;

    QLabel *m_titleLabel = nullptr;
    class StatusDot *m_statusDot = nullptr;
    QComboBox *m_levelFilter = nullptr;
    QPushButton *m_autoScrollButton = nullptr;
    QPushButton *m_clearButton = nullptr;
    QPushButton *m_fullLogButton = nullptr;
    QLineEdit *m_filterEdit = nullptr;
    QLabel *m_subtitleLabel = nullptr;
    QWidget *m_errorBanner = nullptr;
    QLabel *m_errorLabel = nullptr;
    QPlainTextEdit *m_logView = nullptr;

    bool m_autoScroll = true;
    int m_renderedCount = 0;
    QTimer *m_filterDebounce = nullptr;
    QString m_cachedErrorSummary;
};

} // namespace jrunner
