#include "process_task.h"

#include "debug_log.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>

#include <csignal>
#include <sys/types.h>
#include <unistd.h>

namespace jrunner {

namespace {
/// 构造只有 kind 的 TaskState(避免聚合初始化告警)
TaskState makeState(TaskState::Kind kind)
{
    TaskState state;
    state.kind = kind;
    return state;
}

/// 一条日志编码为 JSON-lines 记录(控制字符被转义,保证一行一条)
QByteArray encodeRecord(const LogLine &line)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("t"), line.timestamp.toMSecsSinceEpoch());
    obj.insert(QStringLiteral("l"), static_cast<int>(line.level));
    obj.insert(QStringLiteral("x"), line.text);
    return QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
}

/// 解析一条完整记录;失败返回 false
bool decodeRecord(const QByteArray &raw, LogLine &out)
{
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isObject())
        return false;
    const QJsonObject obj = doc.object();
    out.text = obj.value(QStringLiteral("x")).toString();
    const int lv = obj.value(QStringLiteral("l")).toInt(static_cast<int>(LogLevel::Plain));
    out.level = (lv >= 0 && lv <= 4) ? static_cast<LogLevel>(lv) : LogLevel::Plain;
    out.timestamp = QDateTime::fromMSecsSinceEpoch(obj.value(QStringLiteral("t")).toVariant().toLongLong());
    return true;
}

bool levelMatches(LogLevel level, int mode)
{
    switch (mode) {
    case 1: return level == LogLevel::Error;
    case 2: return level == LogLevel::Error || level == LogLevel::Warn;
    default: return true;
    }
}

const char *levelName(LogLevel level)
{
    switch (level) {
    case LogLevel::Error: return "ERROR";
    case LogLevel::Warn: return "WARN";
    case LogLevel::Info: return "INFO";
    case LogLevel::Debug: return "DEBUG";
    default: return "PLAIN";
    }
}
} // namespace

LogLevel detectLogLevel(const QString &line)
{
    const QString l = line.toUpper();
    if (l.contains(QLatin1String("ERROR")) || l.contains(QLatin1String("EXCEPTION"))
        || l.contains(QLatin1String("FAILED")) || l.contains(QLatin1String("FATAL")))
        return LogLevel::Error;
    if (l.contains(QLatin1String("WARN")))
        return LogLevel::Warn;
    if (l.contains(QLatin1String("DEBUG")) || l.contains(QLatin1String("TRACE")))
        return LogLevel::Debug;
    if (l.contains(QLatin1String("INFO")))
        return LogLevel::Info;
    return LogLevel::Plain;
}

ProcessTask::ProcessTask(const PlannedCommand &command, const QString &title,
                         const QString &subtitle, int maxLines, QObject *parent)
    : QObject(parent)
    , m_command(command)
    , m_title(title)
    , m_subtitle(subtitle)
    , m_maxLines(maxLines)
{
}

ProcessTask::~ProcessTask()
{
    m_stopRequested = true;
    if (m_process && m_process->state() != QProcess::NotRunning) {
        const auto pid = static_cast<pid_t>(m_process->processId());
        if (pid > 0)
            ::kill(-pid, SIGTERM);
        if (!m_process->waitForFinished(500) && pid > 0)
            ::kill(-pid, SIGKILL);
    }
    // 静默冲刷剩余批次到磁盘(不发任何信号),保证"保留全部日志"
    if (!m_pending.isEmpty()) {
        writeBatchToDisk(m_pending);
        m_pending.clear();
    }
    if (m_logFile.isOpen())
        m_logFile.close();
}

void ProcessTask::setState(const TaskState &state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged();
}

void ProcessTask::start()
{
    if (m_state.kind != TaskState::Idle)
        return;
    m_preparing = !m_command.preparationArguments.isEmpty();
    setState(makeState(TaskState::Starting));
    startProcess();
}

void ProcessTask::startProcess()
{
    if (m_stopRequested) {
        setState(makeState(TaskState::Success));
        return;
    }
    auto *p = new QProcess(this);
    p->setProgram(m_command.executable);
    const QStringList args = m_preparing ? m_command.preparationArguments : m_command.arguments;
    p->setArguments(args);
    p->setChildProcessModifier([]() { ::setsid(); });
    p->setWorkingDirectory(m_command.workingDirectory);
    p->setProcessChannelMode(QProcess::MergedChannels);

    // 合并环境变量
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (auto it = m_command.environment.constBegin(); it != m_command.environment.constEnd(); ++it)
        env.insert(it.key(), it.value());
    env.insert(QStringLiteral("TERM"), QStringLiteral("dumb"));      // 关闭 ANSI 颜色/光标控制
    env.insert(QStringLiteral("CI"), QStringLiteral("false"));
    p->setProcessEnvironment(env);

    m_process = p;
    if (m_preparing)
        appendLine(QStringLiteral("正在准备 Maven 模块依赖，完成后自动启动目标模块..."), LogLevel::Info);
    appendLine(QStringLiteral("$ ") + m_command.displayForArguments(args), LogLevel::Debug);

    connect(p, &QProcess::started, this, [this, p]() {
        if (m_stopRequested) {
            ::kill(-static_cast<pid_t>(p->processId()), SIGTERM);
        } else if (!m_preparing) {
            setState(makeState(TaskState::Running));
        }
    });

    connect(p, &QProcess::readyReadStandardOutput, this, [this, p]() {
        m_readBuffer.append(p->readAllStandardOutput());

        // 流式按行拆分
        int idx;
        while ((idx = m_readBuffer.indexOf('\n')) >= 0) {
            const QByteArray lineData = m_readBuffer.left(idx);
            m_readBuffer.remove(0, idx + 1);
            const QString raw = QString::fromUtf8(lineData);
            const QString clean = stripAnsi(raw);
            appendLine(clean, detectLogLevel(clean));
        }
        // 防止超长单行撑爆内存
        if (m_readBuffer.size() > 1000000) {
            const QString raw = QString::fromUtf8(m_readBuffer);
            m_readBuffer.clear();
            const QString clean = stripAnsi(raw);
            appendLine(clean, detectLogLevel(clean));
        }
    });

    // QProcess::finished 即 terminationHandler:守护进程继承管道也不会卡状态
    connect(p, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        handleProcessFinished(exitCode, status);
    });

    connect(p, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            const QString msg = QStringLiteral("启动失败: 无法执行 %1").arg(m_command.executable);
            appendLine(msg, LogLevel::Error);
            setState(TaskState{ TaskState::Failed, msg });
        }
    });

    p->start();
    if (p->state() == QProcess::NotRunning) {
        setState(TaskState{ TaskState::Failed, QStringLiteral("启动失败") });
        return;
    }
}

void ProcessTask::handleProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    // 冲刷残余缓冲
    if (!m_readBuffer.isEmpty()) {
        const QString raw = QString::fromUtf8(m_readBuffer);
        m_readBuffer.clear();
        const QString clean = stripAnsi(raw);
        if (!clean.isEmpty())
            appendLine(clean, detectLogLevel(clean));
    }
    flushNow();
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
    if (m_preparing && !m_stopRequested && status == QProcess::NormalExit && exitCode == 0) {
        m_preparing = false;
        appendLine(QStringLiteral("模块依赖已准备完成，正在启动服务..."), LogLevel::Info);
        startProcess();
        return;
    }

    if (m_stopRequested) {
        setState(makeState(TaskState::Success));
        appendLine(QStringLiteral("进程已停止"), LogLevel::Warn);
    } else if (status == QProcess::NormalExit && exitCode == 0) {
        setState(makeState(TaskState::Success));
        appendLine(QStringLiteral("✅ 执行成功"), LogLevel::Info);
    } else if (status == QProcess::CrashExit) {
        const QString msg = QStringLiteral("进程被信号终止");
        setState(TaskState{ TaskState::Failed, msg });
        appendLine(QStringLiteral("❌ 执行失败,") + msg, LogLevel::Error);
    } else {
        const QString msg = m_preparing
            ? QStringLiteral("依赖准备失败，退出码 %1；未启动目标模块").arg(exitCode)
            : QStringLiteral("退出码 %1").arg(exitCode);
        setState(TaskState{ TaskState::Failed, msg });
        appendLine(QStringLiteral("❌ 执行失败,%1").arg(msg), LogLevel::Error);
    }
    flushNow();
}

void ProcessTask::stop()
{
    if (!m_state.isActive() || m_stopRequested)
        return;
    m_stopRequested = true;
    setState(makeState(TaskState::Stopping));
    appendLine(QStringLiteral("正在停止进程..."), LogLevel::Warn);
    if (!m_process)
        return;
    const QPointer<QProcess> process = m_process;
    const auto pid = static_cast<pid_t>(process->processId());
    // Only signal the process group we created, never an unrelated process on the same port.
    if (pid > 0)
        ::kill(-pid, SIGTERM);
    QTimer::singleShot(5000, this, [this, process]() {
        if (process && process->state() != QProcess::NotRunning && process->processId() > 0) {
            ::kill(-static_cast<pid_t>(process->processId()), SIGKILL);
            appendLine(QStringLiteral("进程已强制结束"), LogLevel::Error);
        }
    });
}

void ProcessTask::clearLogs()
{
    m_pending.clear();
    m_lines.clear();
    if (m_logFile.isOpen())
        m_logFile.close();
    if (!m_logPath.isEmpty())
        QFile::remove(m_logPath);
    m_totalLines = 0;
    emit logsCleared();
}

void ProcessTask::appendLine(const QString &text, LogLevel level)
{
    m_pending.append(LogLine{ text, level, QDateTime::currentDateTime() });
    if (!m_flushScheduled) {
        m_flushScheduled = true;
        // 50ms 内合并刷新,避免高频 dispatch 拖垮主线程
        QTimer::singleShot(50, this, [this]() { flushNow(); });
    }
}

void ProcessTask::flushNow()
{
    m_flushScheduled = false;
    if (m_pending.isEmpty())
        return;
    writeBatchToDisk(m_pending);
    const int firstIndex = m_lines.size();
    const int count = m_pending.size();
    m_lines.append(m_pending);
    m_pending.clear();
    if (m_lines.size() > m_maxLines) {
        const int overflow = m_lines.size() - m_maxLines;
        m_lines.remove(0, overflow);
        emit linesAppended(0, m_lines.size());
    } else {
        emit linesAppended(firstIndex, count);
    }
    // 顺带从新日志里识别端口
    if (m_detectedPort == 0) {
        for (const LogLine &l : m_lines) {
            const int p = extractPort(l.text);
            if (p > 0) {
                m_detectedPort = p;
                emit portDetected(p);
                break;
            }
        }
    }
}

void ProcessTask::enableFileLog(const QString &filePath)
{
    m_logPath = filePath;
    m_logDisabled = false;
    m_totalLines = 0;
}

bool ProcessTask::ensureLogFileOpen()
{
    if (m_logDisabled || m_logPath.isEmpty())
        return false;
    if (m_logFile.isOpen())
        return true;
    const QDir dir = QFileInfo(m_logPath).absoluteDir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        debugLog(QStringLiteral("log"), QStringLiteral("无法创建日志目录: %1").arg(dir.absolutePath()));
        m_logDisabled = true;
        return false;
    }
    m_logFile.setFileName(m_logPath);
    if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
        debugLog(QStringLiteral("log"), QStringLiteral("无法打开日志文件: %1").arg(m_logPath));
        m_logDisabled = true;
        return false;
    }
    return true;
}

void ProcessTask::writeBatchToDisk(const QList<LogLine> &batch)
{
    if (batch.isEmpty() || !ensureLogFileOpen())
        return;
    QByteArray data;
    data.reserve(batch.size() * 96);
    for (const LogLine &l : batch)
        data += encodeRecord(l);
    if (m_logFile.write(data) == data.size()) {
        m_logFile.flush();
        m_totalLines += static_cast<quint64>(batch.size());
    } else {
        debugLog(QStringLiteral("log"), QStringLiteral("日志写入失败: %1").arg(m_logPath));
    }
}

quint64 ProcessTask::totalLineCount() const
{
    if (m_logPath.isEmpty() || m_logDisabled)
        return static_cast<quint64>(m_lines.size());
    return m_totalLines;
}

QList<LogLine> ProcessTask::readHistory(quint64 start, quint64 count) const
{
    QList<LogLine> result;
    if (count == 0)
        return result;
    if (m_logPath.isEmpty() || m_logDisabled) {
        const quint64 total = static_cast<quint64>(m_lines.size());
        if (start >= total)
            return result;
        const quint64 end = qMin(total, start + count);
        for (quint64 i = start; i < end; ++i)
            result.append(m_lines.at(static_cast<int>(i)));
        return result;
    }
    QFile file(m_logPath);
    if (!file.open(QIODevice::ReadOnly))
        return result;
    quint64 index = 0;
    const quint64 end = start + count;
    while (!file.atEnd()) {
        const QByteArray raw = file.readLine();
        if (!raw.endsWith('\n'))
            break;      // 撕裂的尾部半行,丢弃
        if (index >= start && index < end) {
            LogLine line;
            if (decodeRecord(raw, line))
                result.append(line);
        }
        ++index;
        if (index >= end)
            break;
    }
    return result;
}

QList<LogLine> ProcessTask::searchHistory(const QString &query, int levelMode, int cap) const
{
    QList<LogLine> result;
    if (cap <= 0)
        return result;
    const QString q = query.trimmed();
    if (m_logPath.isEmpty() || m_logDisabled) {
        for (const LogLine &l : m_lines) {
            if (!levelMatches(l.level, levelMode))
                continue;
            if (!q.isEmpty() && !l.text.contains(q, Qt::CaseInsensitive))
                continue;
            result.append(l);
            if (result.size() >= cap)
                break;
        }
        return result;
    }
    QFile file(m_logPath);
    if (!file.open(QIODevice::ReadOnly))
        return result;
    while (!file.atEnd()) {
        const QByteArray raw = file.readLine();
        if (!raw.endsWith('\n'))
            break;
        LogLine line;
        if (!decodeRecord(raw, line))
            continue;
        if (!levelMatches(line.level, levelMode))
            continue;
        if (!q.isEmpty() && !line.text.contains(q, Qt::CaseInsensitive))
            continue;
        result.append(line);
        if (result.size() >= cap)
            break;
    }
    return result;
}

bool ProcessTask::exportHistoryToFile(const QString &logPath, const QString &targetPath)
{
    QFile in(logPath);
    if (!in.open(QIODevice::ReadOnly))
        return false;
    QFile out(targetPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    while (!in.atEnd()) {
        const QByteArray raw = in.readLine();
        if (!raw.endsWith('\n'))
            break;
        LogLine line;
        if (!decodeRecord(raw, line))
            continue;
        const QString ts = line.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
        const QString rendered = QStringLiteral("%1 [%2] %3\n")
                                     .arg(ts, QLatin1String(levelName(line.level)), line.text);
        out.write(rendered.toUtf8());
    }
    out.flush();
    return true;
}

int ProcessTask::extractPort(const QString &text)
{
    // 匹配: on port 8080 / with port(s): 8080 / port: 8080
    static const QRegularExpression re(
        QStringLiteral("\\bport(?:\\(s\\))?[:\\s]+(\\d{2,5})\\b"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(text);
    if (!m.hasMatch())
        return 0;
    bool ok = false;
    const int p = m.captured(1).toInt(&ok);
    if (!ok || p <= 0 || p > 65535)
        return 0;
    return p;
}

QString ProcessTask::stripAnsi(const QString &s)
{
    if (!s.contains(QLatin1Char('\u001B')))
        return s;
    static const QRegularExpression re(QStringLiteral("[\\x1B]\\[[0-9;?]*[a-zA-Z]"));
    QString result = s;
    result.remove(re);
    return result;
}

bool ProcessTask::isJavaProcess(qint64 pid)
{
    // Linux:直接读 /proc/<pid>/comm,不起进程
    QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
    if (comm.open(QIODevice::ReadOnly)) {
        const QString name = QString::fromUtf8(comm.readAll()).toLower();
        return name.contains(QLatin1String("java"));
    }
    return false;
}

namespace {

/// 解析 /proc/net/tcp(6) 里监听端口的 socket inode
QList<qint64> listenInodesForPort(int port)
{
    QList<qint64> inodes;
    const QString portHex = QString::number(port, 16).rightJustified(4, QLatin1Char('0'));

    for (const char *path : { "/proc/net/tcp", "/proc/net/tcp6" }) {
        QFile file(QString::fromLatin1(path));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QStringList lines = QString::fromUtf8(file.readAll())
            .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (int i = 1; i < lines.size(); ++i) {     // 跳过表头
            const QStringList cols = lines[i].simplified().split(QLatin1Char(' '));
            if (cols.size() < 10)
                continue;
            // local_address 形如 "0100007F:1F90",state "0A" = LISTEN
            const QStringList local = cols.value(1).split(QLatin1Char(':'));
            if (local.size() != 2 || local.value(1) != portHex)
                continue;
            if (cols.value(3) != QLatin1String("0A"))
                continue;
            bool ok = false;
            const qint64 inode = cols.value(9).toLongLong(&ok);
            if (ok && inode > 0)
                inodes.append(inode);
        }
    }
    return inodes;
}

} // namespace

QList<qint64> ProcessTask::listeningPids(int port)
{
    // 纯 /proc 实现,不依赖 lsof / ss
    const QList<qint64> inodes = listenInodesForPort(port);
    if (inodes.isEmpty())
        return {};

    QSet<qint64> inodeSet(inodes.begin(), inodes.end());
    QList<qint64> pids;
    QDir proc(QStringLiteral("/proc"));
    const QStringList entries = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &e : entries) {
        bool ok = false;
        const qint64 pid = e.toLongLong(&ok);
        if (!ok || pid <= 0)
            continue;
        QDir fdDir(QStringLiteral("/proc/%1/fd").arg(pid));
        const auto fds = fdDir.entryList(QDir::Files);
        for (const QString &fd : fds) {
            const QString target = QFile::symLinkTarget(
                QStringLiteral("/proc/%1/fd/%2").arg(pid).arg(fd));
            // 形如 "socket:[123456]"
            if (!target.startsWith(QLatin1String("socket:")))
                continue;
            const QString num = target.mid(8, target.size() - 9);
            bool numOk = false;
            const qint64 inode = num.toLongLong(&numOk);
            if (numOk && inodeSet.contains(inode)) {
                pids.append(pid);
                break;
            }
        }
    }
    return pids;
}

} // namespace jrunner
