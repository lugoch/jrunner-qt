#include "git_info.h"

#include "debug_log.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QtConcurrent/QtConcurrentRun>

namespace jrunner {

namespace {

QString readFileIfExists(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(file.readAll());
}

/// git 二进制位置:优先用户 PATH,退回系统位置
QString gitBinary()
{
    static const QStringList candidates = {
        QStringLiteral("/usr/bin/git"),
        QStringLiteral("/usr/local/bin/git"),
    };
    for (const QString &c : candidates) {
        QFileInfo info(c);
        if (info.isFile() && info.isExecutable())
            return c;
    }
    return QStringLiteral("git");
}

struct GitSnapshot {
    GitStatus status;
    QStringList branches;
};

GitSnapshot readSnapshot(const QString &path)
{
    GitSnapshot result;
    result.status = GitReader::readHead(path);
    if (result.status.isRepo()) {
        const auto workingTree = GitReader::readWorkingTree(path);
        result.status.isDirty = workingTree.first;
        result.status.changedCount = workingTree.second;
        result.branches = GitReader::localBranches(path);
    }
    return result;
}

struct CheckoutResult {
    bool succeeded = false;
    QString error;
    GitSnapshot snapshot;
};

} // namespace

namespace GitReader {

QString repoRoot(const QString &path)
{
    QString cur = path;
    for (int depth = 0; depth < 16; ++depth) {
        if (QFileInfo::exists(cur + QStringLiteral("/.git")))
            return cur;
        const QDir dir(cur);
        const QString parent = dir.absolutePath() == QStringLiteral("/")
            ? QString()
            : QFileInfo(cur).absolutePath();
        if (parent.isEmpty() || parent == cur)
            return QString();
        cur = parent;
    }
    return QString();
}

QString gitDir(const QString &repoPath)
{
    const QString root = repoRoot(repoPath);
    if (root.isEmpty())
        return QString();
    const QString dotGit = root + QStringLiteral("/.git");
    QFileInfo info(dotGit);
    if (!info.exists())
        return QString();
    if (info.isDir())
        return dotGit;

    // worktree / submodule:.git 是一个文件,内含 gitdir: <path>
    const QString content = readFileIfExists(dotGit);
    const QStringList lines = content.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString s = line.trimmed();
        if (!s.startsWith(QStringLiteral("gitdir:")))
            continue;
        QString p = s.mid(QStringLiteral("gitdir:").size()).trimmed();
        if (p.isEmpty())
            return QString();
        if (!p.startsWith(QLatin1Char('/')))
            p = root + QLatin1Char('/') + p;
        return p;
    }
    return QString();
}

GitStatus readHead(const QString &repoPath)
{
    const QString dir = gitDir(repoPath);
    if (dir.isEmpty())
        return GitStatus::none();
    const QString head = readFileIfExists(dir + QStringLiteral("/HEAD"));
    if (head.isEmpty())
        return GitStatus::none();

    const QString trimmed = head.trimmed();
    if (trimmed.startsWith(QStringLiteral("ref:"))) {
        QString ref = trimmed.mid(4).trimmed();
        const QString prefix = QStringLiteral("refs/heads/");
        const QString name = ref.startsWith(prefix) ? ref.mid(prefix.size()) : ref;
        GitStatus st;
        st.branch = name;
        st.isDetached = false;
        return st;
    }
    // detached HEAD:HEAD 里直接是完整 SHA
    if (trimmed.size() >= 7) {
        GitStatus st;
        st.branch = trimmed.left(7);
        st.isDetached = true;
        return st;
    }
    return GitStatus::none();
}

QPair<bool, int> readWorkingTree(const QString &repoPath)
{
    QProcess p;
    p.setProgram(gitBinary());
    p.setArguments({ QStringLiteral("-C"), repoPath, QStringLiteral("status"),
                     QStringLiteral("--porcelain"),
                     QStringLiteral("--untracked-files=no"),
                     QStringLiteral("--ignore-submodules=dirty") });
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    env.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
    p.setProcessEnvironment(env);
    p.start();
    if (!p.waitForFinished(10000))
        return qMakePair(false, 0);
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    const int n = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts).size();
    return qMakePair(n > 0, n);
}

QStringList localBranches(const QString &repoPath)
{
    const QString dir = gitDir(repoPath);
    if (dir.isEmpty())
        return {};

    QSet<QString> names;

    // 松散引用
    const QString heads = dir + QStringLiteral("/refs/heads");
    QDir headsDir(heads);
    if (headsDir.exists()) {
        QDirIterator it(heads, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            names.insert(headsDir.relativeFilePath(it.filePath()));
        }
    }

    // 打包引用:sha refs/heads/xxx
    const QString packed = readFileIfExists(dir + QStringLiteral("/packed-refs"));
    for (const QString &rawLine : packed.split(QLatin1Char('\n'))) {
        const QString s = rawLine.trimmed();
        if (s.isEmpty() || s.startsWith(QLatin1Char('#')) || s.startsWith(QLatin1Char('^')))
            continue;
        const int idx = s.indexOf(QStringLiteral("refs/heads/"));
        if (idx < 0)
            continue;
        const QString name = s.mid(idx + QStringLiteral("refs/heads/").size());
        if (!name.isEmpty())
            names.insert(name);
    }

    QStringList result(names.constBegin(), names.constEnd());
    std::sort(result.begin(), result.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return result;
}

QPair<bool, QString> checkout(const QString &repoPath, const QString &branch)
{
    QProcess p;
    p.setProgram(gitBinary());
    p.setArguments({ QStringLiteral("-C"), repoPath, QStringLiteral("checkout"), branch });
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    p.setProcessEnvironment(env);
    p.start();
    if (!p.waitForStarted(5000))
        return qMakePair(false, QStringLiteral("无法启动 git"));
    if (!p.waitForFinished(60000))
        return qMakePair(false, QStringLiteral("git checkout 超时"));

    if (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0)
        return qMakePair(true, QString());

    const QString err = QString::fromUtf8(p.readAllStandardError());
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    QString msg = (err + out).trimmed();
    if (msg.isEmpty())
        msg = QStringLiteral("git checkout 退出码 %1").arg(p.exitCode());
    return qMakePair(false, msg);
}

} // namespace GitReader

GitMonitor::GitMonitor(QObject *parent)
    : QObject(parent)
{
}

GitStatus GitMonitor::status(const QString &path) const
{
    return m_statuses.value(path);
}

QStringList GitMonitor::branches(const QString &path) const
{
    return m_branches.value(path);
}

void GitMonitor::refresh(const QString &path, bool force)
{
    if (path.isEmpty() || path == m_checkoutPath || m_inFlight.contains(path))
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!force && m_lastRunMs.contains(path) && now - m_lastRunMs.value(path) < 2000)
        return;
    m_inFlight.insert(path);

    const quint64 revision = m_revisions.value(path);
    const QPointer<GitMonitor> guard(this);
    auto *watcher = new QFutureWatcher<GitSnapshot>(this);
    connect(watcher, &QFutureWatcher<GitSnapshot>::finished, this,
            [guard, watcher, path, revision]() {
                const GitSnapshot result = watcher->result();
                watcher->deleteLater();
                if (!guard)
                    return;
                guard->m_inFlight.remove(path);
                if (guard->m_revisions.value(path) != revision)
                    return;
                const bool branchesChanged = guard->m_branches.value(path) != result.branches;
                guard->m_branches.insert(path, result.branches);
                guard->finish(path, result.status);
                if (guard && branchesChanged)
                    emit guard->branchesUpdated(path);
            });
    // 工作线程只持有值;watcher 的 QObject 上下文负责接收结果和生命周期。
    watcher->setFuture(QtConcurrent::run([path]() { return readSnapshot(path); }));
}

void GitMonitor::loadBranches(const QString &path)
{
    if (path.isEmpty() || path == m_checkoutPath)
        return;
    const quint64 revision = m_revisions.value(path);
    const QPointer<GitMonitor> guard(this);
    auto *watcher = new QFutureWatcher<QStringList>(this);
    connect(watcher, &QFutureWatcher<QStringList>::finished, this,
            [guard, watcher, path, revision]() {
                const QStringList list = watcher->result();
                watcher->deleteLater();
                if (!guard || guard->m_revisions.value(path) != revision)
                    return;
                if (guard->m_branches.value(path) != list) {
                    guard->m_branches.insert(path, list);
                    emit guard->branchesUpdated(path);
                }
            });
    watcher->setFuture(QtConcurrent::run([path]() { return GitReader::localBranches(path); }));
}

void GitMonitor::refreshBranches(const QStringList &paths)
{
    QHash<QString, quint64> revisions;
    for (const QString &path : paths) {
        if (!path.isEmpty() && path != m_checkoutPath)
            revisions.insert(path, m_revisions.value(path));
    }
    if (revisions.isEmpty())
        return;

    using StatusMap = QHash<QString, GitStatus>;
    const QPointer<GitMonitor> guard(this);
    auto *watcher = new QFutureWatcher<StatusMap>(this);
    connect(watcher, &QFutureWatcher<StatusMap>::finished, this,
            [guard, watcher, revisions]() {
                const StatusMap result = watcher->result();
                watcher->deleteLater();
                for (auto it = result.constBegin(); it != result.constEnd(); ++it) {
                    if (!guard)
                        return;
                    if (guard->m_revisions.value(it.key()) != revisions.value(it.key()))
                        continue;
                    GitStatus status = it.value();
                    if (status.isRepo()) {
                        // 只读 HEAD 的结果不覆盖最近一次完整刷新得到的改动状态。
                        const GitStatus current = guard->m_statuses.value(it.key());
                        status.isDirty = current.isDirty;
                        status.changedCount = current.changedCount;
                    }
                    if (guard->m_statuses.value(it.key()) != status) {
                        guard->m_statuses.insert(it.key(), status);
                        emit guard->statusUpdated(it.key());
                    }
                }
            });
    watcher->setFuture(QtConcurrent::run([paths = revisions.keys()]() {
        StatusMap result;
        for (const QString &path : paths)
            result.insert(path, GitReader::readHead(path));
        return result;
    }));
}

void GitMonitor::checkoutBranch(const QString &path, const QString &branch)
{
    if (m_checkingOut || path.isEmpty() || branch.isEmpty())
        return;
    m_checkingOut = true;
    m_checkoutPath = path;
    ++m_revisions[path];
    const QPointer<GitMonitor> guard(this);
    emit checkingOutChanged();
    if (!guard)
        return;

    auto *watcher = new QFutureWatcher<CheckoutResult>(this);
    connect(watcher, &QFutureWatcher<CheckoutResult>::finished, this,
            [guard, watcher, path, branch]() {
                const CheckoutResult result = watcher->result();
                watcher->deleteLater();
                if (!guard)
                    return;
                if (result.succeeded) {
                    const bool branchesChanged =
                        guard->m_branches.value(path) != result.snapshot.branches;
                    guard->m_branches.insert(path, result.snapshot.branches);
                    guard->finish(path, result.snapshot.status);
                    if (!guard)
                        return;
                    if (branchesChanged)
                        emit guard->branchesUpdated(path);
                    if (!guard)
                        return;
                }
                guard->m_checkingOut = false;
                guard->m_checkoutPath.clear();
                emit guard->checkingOutChanged();
                if (!guard)
                    return;
                if (result.succeeded)
                    emit guard->checkoutSucceeded(path, branch);
                else
                    emit guard->checkoutFailed(QStringLiteral("切换到 %1 失败").arg(branch),
                                               result.error);
            });
    watcher->setFuture(QtConcurrent::run([path, branch]() {
        const auto outcome = GitReader::checkout(path, branch);
        CheckoutResult result;
        result.succeeded = outcome.first;
        result.error = outcome.second;
        // 成功信号发出前完成状态读取,不会被尚未结束的旧刷新覆盖。
        if (result.succeeded)
            result.snapshot = readSnapshot(path);
        return result;
    }));
}

void GitMonitor::finish(const QString &path, const GitStatus &st)
{
    m_lastRunMs.insert(path, QDateTime::currentMSecsSinceEpoch());
    if (m_statuses.value(path) != st) {
        m_statuses.insert(path, st);
        emit statusUpdated(path);
    }
}

} // namespace jrunner
