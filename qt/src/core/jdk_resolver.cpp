#include "jdk_resolver.h"

#include "debug_log.h"
#include "shell_env.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>

namespace jrunner {
namespace JDKResolver {

namespace {

QMutex &cacheMutex()
{
    static QMutex mutex;
    return mutex;
}

JDKList g_cache;
bool g_loaded = false;

/// 从目录名猜测厂商,如 "temurin-17-jdk" → "Temurin"
QString guessVendorFromName(const QString &name)
{
    const QString lower = name.toLower();
    if (lower.contains(QLatin1String("temurin")) || lower.contains(QLatin1String("adoptium")))
        return QStringLiteral("Eclipse Temurin");
    if (lower.contains(QLatin1String("zulu")))
        return QStringLiteral("Azul Zulu");
    if (lower.contains(QLatin1String("corretto")))
        return QStringLiteral("Amazon Corretto");
    if (lower.contains(QLatin1String("graalvm")))
        return QStringLiteral("GraalVM");
    if (lower.contains(QLatin1String("liberica")))
        return QStringLiteral("BellSoft Liberica");
    if (lower.contains(QLatin1String("openjdk")))
        return QStringLiteral("OpenJDK");
    if (lower.contains(QLatin1String("oracle")))
        return QStringLiteral("Oracle");
    if (lower.contains(QLatin1String("microsoft")))
        return QStringLiteral("Microsoft");
    if (lower.contains(QLatin1String("semeru")) || lower.contains(QLatin1String("ibm")))
        return QStringLiteral("IBM Semeru");
    return QStringLiteral("Java");
}

/// 校验目录是有效的 JDK 根(有 bin/java 与 release 文件)
bool looksLikeJdkHome(const QString &path)
{
    return QFileInfo::exists(path + QStringLiteral("/bin/java"))
        && QFileInfo::exists(path + QStringLiteral("/release"));
}

/// 从 PATH 上 / JAVA_HOME 的 java 可执行文件反解 JDK 根目录
std::optional<QString> resolveJavaHomeFromBinary(const QString &javaBinary)
{
    // 通过 readlink -f 解析符号链接链(如 /etc/alternatives/java → /usr/lib/jvm/.../bin/java)
    QString resolved = javaBinary;
    for (int i = 0; i < 8; ++i) {
        const QString target = QFile::symLinkTarget(resolved);
        if (target.isEmpty())
            break;
        resolved = target;
    }
    QDir dir = QFileInfo(resolved).dir();
    // 期望布局 <jdkHome>/bin/java,java17 某些发行版为 <jdkHome>/bin/../lib/... 一律向上找
    if (dir.dirName() == QStringLiteral("bin")) {
        dir.cdUp();
        if (looksLikeJdkHome(dir.absolutePath()) || QFileInfo::exists(dir.absolutePath() + QStringLiteral("/bin/java")))
            return dir.absolutePath();
    }
    return std::nullopt;
}

/// 当前默认 JDK(系统 java 所在的 JDK 根)
std::optional<QString> currentJavaHome()
{
    const QString javaHomeEnv = QProcessEnvironment::systemEnvironment().value(QStringLiteral("JAVA_HOME"));
    if (!javaHomeEnv.isEmpty() && QFileInfo::exists(javaHomeEnv + QStringLiteral("/bin/java")))
        return javaHomeEnv;

    const QString loginWhich = Shell::which(QStringLiteral("java"));
    if (!loginWhich.isEmpty())
        return resolveJavaHomeFromBinary(loginWhich);

    // /etc/alternatives/java(Debian 系)
    if (QFileInfo::exists(QStringLiteral("/etc/alternatives/java")))
        return resolveJavaHomeFromBinary(QStringLiteral("/etc/alternatives/java"));
    return std::nullopt;
}

void appendCandidate(JDKList &list, const QString &home, bool fromScan)
{
    if (home.isEmpty() || !QFileInfo::exists(home + QStringLiteral("/bin/java")))
        return;
    // 符号链接别名(如 /usr/lib/jvm/default-java → java-17-openjdk)归一到真实路径
    QString canonical = QFileInfo(home).canonicalFilePath();
    if (canonical.isEmpty())
        canonical = home;

    int major = 0;
    QString full;
    const auto parsed = parseReleaseFile(canonical);
    if (parsed) {
        major = parsed->first;
        full = parsed->second;
    }
    if (major == 0) {
        // release 文件缺失时用目录名猜版本(如 java-17-openjdk-amd64)
        static const QRegularExpression re(QStringLiteral("(?:^|[^0-9])(1\\.|)(\\d{1,2})"));
        const auto m = re.match(QFileInfo(canonical).fileName());
        if (m.hasMatch())
            major = m.captured(2).toInt();
    }
    if (major == 0)
        return;      // 版本不可识别,跳过

    const QString vendor = guessVendorFromName(canonical);
    const QString fullVersion = full.isEmpty() ? QString::number(major) : full;
    // 去重:同一真实路径,或同版本+同厂商(不同别名/拷贝)只保留首次出现
    for (const JDK &existing : list) {
        if (existing.home == canonical)
            return;
        if (existing.version == major && existing.fullVersion == fullVersion
            && existing.vendor == vendor)
            return;
    }

    JDK jdk;
    jdk.version = major;
    jdk.fullVersion = fullVersion;
    jdk.vendor = vendor;
    jdk.home = canonical;
    jdk.isCurrent = false;
    Q_UNUSED(fromScan);
    list.append(jdk);
}

JDKList detectJDKs()
{
    JDKList found;
    const QString home = QDir::homePath();

    // 1. $JAVA_HOME
    const QString javaHomeEnv = QProcessEnvironment::systemEnvironment().value(QStringLiteral("JAVA_HOME"));
    if (!javaHomeEnv.isEmpty())
        appendCandidate(found, javaHomeEnv, true);

    // 2. /usr/lib/jvm/*(发行版主位置)
    static const QStringList systemRoots = {
        QStringLiteral("/usr/lib/jvm"),
        QStringLiteral("/usr/lib64/jvm"),
    };
    for (const QString &root : systemRoots) {
        QDir d(root);
        const QStringList entries = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &e : entries) {
            const QString candidate = root + QLatin1Char('/') + e;
            // 直接是 JDK 根,或含一层内部目录(如 ~/.jdks/jdk-17/bin 布局罕见,但 deb 解包目录有时多一层)
            appendCandidate(found, candidate, true);
            if (!looksLikeJdkHome(candidate)) {
                QDir inner(candidate);
                const QStringList innerEntries = inner.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                for (const QString &ie : innerEntries)
                    appendCandidate(found, candidate + QLatin1Char('/') + ie, true);
            }
        }
    }

    // 3. ~/.jdks(IntelliJ 下载的 JDK)
    QDir jdks(home + QStringLiteral("/.jdks"));
    const QStringList jdksEntries = jdks.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &e : jdksEntries)
        appendCandidate(found, jdks.absoluteFilePath(e), true);

    // 4. ~/.sdkman/candidates/java/*
    QDir sdkman(home + QStringLiteral("/.sdkman/candidates/java"));
    const QStringList sdkmanEntries = sdkman.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &e : sdkmanEntries) {
        if (e == QLatin1String("current"))
            continue;
        appendCandidate(found, sdkman.absoluteFilePath(e), true);
    }

    // 5. ~/.asdf/installs/java/*
    QDir asdf(home + QStringLiteral("/.asdf/installs/java"));
    const QStringList asdfEntries = asdf.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &e : asdfEntries)
        appendCandidate(found, asdf.absoluteFilePath(e), true);

    // 6. 当前默认(可能是符号链接未覆盖的安装)
    if (const auto current = currentJavaHome())
        appendCandidate(found, *current, false);

    // 标记系统默认
    if (const auto current = currentJavaHome()) {
        QString canonicalCurrent = QFileInfo(*current).canonicalFilePath();
        if (canonicalCurrent.isEmpty())
            canonicalCurrent = *current;
        for (JDK &jdk : found) {
            if (jdk.home == canonicalCurrent)
                jdk.isCurrent = true;
        }
    }

    // 按主版本降序
    std::sort(found.begin(), found.end(), [](const JDK &a, const JDK &b) {
        return a.version > b.version;
    });
    debugLog(QStringLiteral("jdk"), QStringLiteral("发现 %1 个 JDK").arg(found.size()));
    return found;
}

} // namespace

std::optional<QPair<int, QString>> parseReleaseFile(const QString &jdkHome)
{
    QFile file(jdkHome + QStringLiteral("/release"));
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QString content = QString::fromUtf8(file.readAll());

    static const QRegularExpression versionRe(
        QStringLiteral("^JAVA_VERSION=\"?([0-9]+(?:\\.[0-9_]+(?:\\.[0-9_]+)?)*)\"?"),
        QRegularExpression::MultilineOption);
    const auto m = versionRe.match(content);
    if (!m.hasMatch())
        return std::nullopt;

    const QString versionToken = m.captured(1);
    const QStringList parts = versionToken.split(QRegularExpression(QStringLiteral("[._]")));
    bool ok = false;
    const int first = parts.value(0).toInt(&ok);
    if (!ok || first <= 0)
        return std::nullopt;
    // 旧布局 1.8.0_x → 主版本 8
    const int major = first == 1 ? parts.value(1).toInt() : first;
    QString full = versionToken;
    full.replace(QLatin1Char('_'), QLatin1Char('.'));
    return qMakePair(major, full);
}

JDKList installedJDKs(bool forceRefresh)
{
    QMutexLocker locker(&cacheMutex());
    if (!forceRefresh && g_loaded)
        return g_cache;
    g_cache = detectJDKs();
    g_loaded = true;
    return g_cache;
}

void invalidateCache()
{
    QMutexLocker locker(&cacheMutex());
    g_cache.clear();
    g_loaded = false;
}

std::optional<JDK> resolve(std::optional<int> version)
{
    const JDKList all = installedJDKs();
    if (all.isEmpty())
        return std::nullopt;
    if (!version || *version <= 0) {
        for (const JDK &jdk : all) {
            if (jdk.isCurrent)
                return jdk;
        }
        return all.first();
    }
    // 精确匹配优先
    for (const JDK &jdk : all) {
        if (jdk.version == *version)
            return jdk;
    }
    // 否则取 >= 需求版本中最低的
    const JDK *best = nullptr;
    for (const JDK &jdk : all) {
        if (jdk.version >= *version && (!best || jdk.version < best->version))
            best = &jdk;
    }
    if (best)
        return *best;
    return all.first();
}

} // namespace JDKResolver
} // namespace jrunner
