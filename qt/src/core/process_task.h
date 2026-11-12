#pragma once

#include "task_planner.h"

#include <QDateTime>
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace jrunner {

// 【进程任务】运行一个命令并实时捕获输出,对应 macOS 版 ProcessTask.swift。
// QProcess 信号驱动,无需手动读线程;50ms 批量刷新避免高频 UI 更新。

/// 日志级别
enum class LogLevel {
    Error,
    Warn,
    Info,
    Debug,
    Plain,
};

LogLevel detectLogLevel(const QString &line);

/// 一条日志
struct LogLine {
    QString text;
    LogLevel level = LogLevel::Plain;
    QDateTime timestamp = QDateTime::currentDateTime();
};

/// 运行中的任务状态
struct TaskState {
    enum Kind {
        Idle,
        Starting,
        Running,
        Stopping,
        Success,
        Failed,
    };
    Kind kind = Idle;
    QString failureMessage;      // 仅 Failed 有效

    bool isActive() const { return kind == Starting || kind == Running || kind == Stopping; }
    bool operator==(const TaskState &o) const { return kind == o.kind && failureMessage == o.failureMessage; }
};

/// 后台任务
class ProcessTask : public QObject {
    Q_OBJECT

public:
    ProcessTask(const PlannedCommand &command, const QString &title,
                const QString &subtitle, int maxLines = 20000,
                QObject *parent = nullptr);
    ~ProcessTask() override;

    const PlannedCommand &command() const { return m_command; }
    QString title() const { return m_title; }
    QString subtitle() const { return m_subtitle; }
    TaskState state() const { return m_state; }
    /// 环形缓冲里的日志(主线程访问)
    const QList<LogLine> &lines() const { return m_lines; }
    /// 从日志里识别到的服务端口(如 "Tomcat started on port 8080")
    int detectedPort() const { return m_detectedPort; }

    void start();
    /// Stops this task's process group, including a running dependency preparation.
    void stop();
    void clearLogs();

    // ---- 全量日志落盘(opt-in;内存仍只保留尾部窗口) ----
    /// 启用追加式 JSON-lines 文件日志,须在 start() 前调用。
    void enableFileLog(const QString &filePath);
    QString logFilePath() const { return m_logPath; }
    /// 已写入磁盘的总行数(O(1));未启用落盘时回退内存窗口大小。
    quint64 totalLineCount() const;
    /// 从磁盘读取 [start, start+count) 区间的历史(未启用时读内存窗口)。
    QList<LogLine> readHistory(quint64 start, quint64 count) const;
    /// 全文搜索:levelMode 0=全部 1=仅错误 2=错误+警告;命中 cap 即止。
    QList<LogLine> searchHistory(const QString &query, int levelMode, int cap) const;
    /// 把整个日志文件导出为纯文本(带 [LEVEL] 标签)。
    static bool exportHistoryToFile(const QString &logPath, const QString &targetPath);

    // 静态工具(测试用)
    static int extractPort(const QString &text);
    static QString stripAnsi(const QString &s);
    static bool isJavaProcess(qint64 pid);
    static QList<qint64> listeningPids(int port);

signals:
    void stateChanged();
    /// 一批日志已追加(主线程)
    void linesAppended(int firstIndex, int count);
    void portDetected(int port);
    void logsCleared();

private:
    void appendLine(const QString &text, LogLevel level);
    void flushNow();
    void setState(const TaskState &state);
    void handleProcessFinished(int exitCode, QProcess::ExitStatus status);
    void startProcess();
    bool ensureLogFileOpen();
    void writeBatchToDisk(const QList<LogLine> &batch);

    PlannedCommand m_command;
    QString m_title;
    QString m_subtitle;
    int m_maxLines;
    TaskState m_state;
    QList<LogLine> m_lines;
    int m_detectedPort = 0;

    QPointer<QProcess> m_process;
    QList<LogLine> m_pending;
    bool m_flushScheduled = false;
    QByteArray m_readBuffer;
    bool m_preparing = false;
    bool m_stopRequested = false;

    QFile m_logFile;
    QString m_logPath;
    quint64 m_totalLines = 0;
    bool m_logDisabled = false;
};

} // namespace jrunner
