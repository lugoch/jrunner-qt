#include "shell_env.h"

#include "debug_log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace jrunner {
namespace Shell {

namespace {

QMutex &cacheMutex()
{
    static QMutex mutex;
    return mutex;
}

bool isValidEnvKey(const QString &key)
{
    if (key.isEmpty())
        return false;
    static const QRegularExpression re(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    return re.match(key).hasMatch();
}

QString defaultShell()
{
    const QString shell = QProcessEnvironment::systemEnvironment().value(QStringLiteral("SHELL"));
    return shell.isEmpty() ? QStringLiteral("/bin/bash") : shell;
}

QHash<QString, QString> g_cachedEnv;
QString g_cachedPath;
bool g_envLoaded = false;
bool g_pathLoaded = false;

} // namespace

std::optional<ShellResult> run(const QString &launchPath, const QStringList &args,
                               const QString &cwd, const QHash<QString, QString> &env)
{
    QProcess p;
    p.setProgram(launchPath);
    p.setArguments(args);
    if (!cwd.isEmpty())
        p.setWorkingDirectory(cwd);
    if (!env.isEmpty()) {
        QProcessEnvironment full = QProcessEnvironment::systemEnvironment();
        for (auto it = env.constBegin(); it != env.constEnd(); ++it)
            full.insert(it.key(), it.value());
        p.setProcessEnvironment(full);
    }

    p.start();
    // 必须并发读取,QProcess 内部已处理管道缓冲,这里等待完成即可
    if (!p.waitForStarted(5000)) {
        debugLog(QStringLiteral("shell"), QStringLiteral("启动失败: %1").arg(launchPath));
        return std::nullopt;
    }
    if (!p.waitForFinished(60000)) {
        p.kill();
        p.waitForFinished(3000);
        return std::nullopt;
    }

    ShellResult result;
    result.exitCode = p.exitCode();
    result.standardOutput = QString::fromUtf8(p.readAllStandardOutput());
    result.standardError = QString::fromUtf8(p.readAllStandardError());
    return result;
}

QString which(const QString &name)
{
    const QStringList dirs = userPath().split(QLatin1Char(':'), Qt::SkipEmptyParts);
    for (const QString &dir : dirs) {
        const QString full = dir + QLatin1Char('/') + name;
        QFileInfo info(full);
        if (info.isExecutable() && info.isFile())
            return full;
    }
    return QString();
}

QHash<QString, QString> userEnvironment()
{
    QMutexLocker locker(&cacheMutex());
    if (g_envLoaded)
        return g_cachedEnv;

    QHash<QString, QString> result;
    if (auto r = run(defaultShell(), { QStringLiteral("-lc"), QStringLiteral("env") })) {
        const QStringList lines = r->standardOutput.split(QRegularExpression(QStringLiteral("[\\r\\n]")));
        for (const QString &line : lines) {
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq <= 0)
                continue;
            const QString key = line.left(eq);
            const QString value = line.mid(eq + 1);
            if (!isValidEnvKey(key))
                continue;
            result.insert(key, value);
        }
    }

    // 这些变量由我们自己控制,不继承
    for (const char *k : { "PWD", "OLDPWD", "SHLVL", "_", "PS1", "TERM", "SHELL", "PATH", "JAVA_HOME" })
        result.remove(QString::fromLatin1(k));

    g_cachedEnv = result;
    g_envLoaded = true;
    return g_cachedEnv;
}

QString userPath()
{
    QMutexLocker locker(&cacheMutex());
    if (g_pathLoaded)
        return g_cachedPath;

    QStringList parts;

    // 1. login shell 的 PATH
    if (auto r = run(defaultShell(), { QStringLiteral("-lc"), QStringLiteral("echo -n $PATH") })) {
        // shell 配置可能有其他输出,取最后一个像 PATH 的行
        const QStringList candidates = r->standardOutput
            .split(QRegularExpression(QStringLiteral("[\\r\\n]")));
        QString last;
        for (const QString &line : candidates) {
            const QString t = line.trimmed();
            if (!t.isEmpty() && t.contains(QLatin1Char('/')))
                last = t;
        }
        if (!last.isEmpty())
            parts = last.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    }

    // 2. 常见安装位置兜底(Linux)
    const QString home = QDir::homePath();
    const QStringList extras = {
        home + QStringLiteral("/.sdkman/candidates/gradle/current/bin"),
        home + QStringLiteral("/.sdkman/candidates/maven/current/bin"),
        home + QStringLiteral("/.sdkman/candidates/java/current/bin"),
        home + QStringLiteral("/.local/bin"),
        QStringLiteral("/usr/local/bin"),
        QStringLiteral("/usr/bin"), QStringLiteral("/bin"),
        QStringLiteral("/usr/sbin"), QStringLiteral("/sbin"),
    };
    for (const QString &e : extras) {
        if (!parts.contains(e) && QFileInfo::exists(e))
            parts.append(e);
    }

    g_cachedPath = parts.join(QLatin1Char(':'));
    g_pathLoaded = true;
    debugLog(QStringLiteral("shell"), QStringLiteral("userPath: %1").arg(g_cachedPath));
    return g_cachedPath;
}

void invalidateCache()
{
    QMutexLocker locker(&cacheMutex());
    g_cachedEnv.clear();
    g_cachedPath.clear();
    g_envLoaded = false;
    g_pathLoaded = false;
}

} // namespace Shell
} // namespace jrunner
