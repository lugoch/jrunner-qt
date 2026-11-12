#include "project_scanner.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QTextStream>

namespace jrunner {
namespace ProjectScanner {

namespace {

/// 读取整个 UTF-8 文本文件;失败返回空
QString readTextFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(file.readAll());
}

QString findFile(const QString &dir, const QStringList &names)
{
    for (const QString &n : names) {
        const QString p = dir + QLatin1Char('/') + n;
        if (QFileInfo::exists(p))
            return p;
    }
    return QString();
}

/// 去掉行内注释(# 或 //),但保留引号内的内容
QString stripComment(const QString &line)
{
    QString result;
    bool inQuote = false;
    QChar quoteChar(QLatin1Char('\''));
    QChar prev(QLatin1Char(' '));

    for (const QChar &ch : line) {
        if (inQuote) {
            result.append(ch);
            if (ch == quoteChar)
                inQuote = false;
        } else {
            if (ch == QLatin1Char('\'') || ch == QLatin1Char('"')) {
                inQuote = true;
                quoteChar = ch;
                result.append(ch);
            } else if (ch == QLatin1Char('#')) {
                break;
            } else if (ch == QLatin1Char('/') && prev == QLatin1Char('/')) {
                // 回退已写入的第二个 '/'
                result.chop(1);
                break;
            } else {
                result.append(ch);
            }
        }
        prev = ch;
    }
    return result.trimmed();
}

/// 提取字符串中所有被单/双引号包裹的内容
QStringList quotedStrings(const QString &s)
{
    QStringList out;
    bool inQuote = false;
    QString cur;
    QChar quoteChar(QLatin1Char('\''));
    for (const QChar &ch : s) {
        if (!inQuote) {
            if (ch == QLatin1Char('\'') || ch == QLatin1Char('"')) {
                inQuote = true;
                quoteChar = ch;
                cur.clear();
            }
        } else {
            if (ch == quoteChar) {
                out.append(cur);
                inQuote = false;
                cur.clear();
            } else {
                cur.append(ch);
            }
        }
    }
    return out;
}

/// 扫描 src/main/resources 下的 application-*.yaml,列出可用 profile
QStringList detectProfiles(const QString &dir)
{
    const QString resDir = dir + QStringLiteral("/src/main/resources");
    QDir d(resDir);
    const QStringList files = d.entryList(QDir::Files);

    QStringList profiles;
    for (const QString &f : files) {
        if (!f.startsWith(QStringLiteral("application-")))
            continue;
        for (const QString &ext : { QStringLiteral(".yaml"), QStringLiteral(".yml"), QStringLiteral(".properties") }) {
            if (!f.endsWith(ext))
                continue;
            const QString name = f.mid(QStringLiteral("application-").size(),
                                       f.size() - QStringLiteral("application-").size() - ext.size());
            if (!name.isEmpty() && !profiles.contains(name))
                profiles.append(name);
        }
    }
    // dev 排前面,便于本地开发
    std::sort(profiles.begin(), profiles.end(), [](const QString &a, const QString &b) {
        if (a == QLatin1String("dev"))
            return true;
        if (b == QLatin1String("dev"))
            return false;
        return a < b;
    });
    return profiles;
}

QString detectLanguageInDir(const QString &dir)
{
    QDir d(dir);
    if (!d.exists())
        return QString();
    const QStringList entries = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    int kt = 0, java = 0;
    for (const QString &e : entries) {
        if (e == QLatin1String("main") || e == QLatin1String("test")) {
            QDir sub(dir + QLatin1Char('/') + e);
            const QStringList subEntries = sub.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString &se : subEntries) {
                if (se == QLatin1String("java"))
                    java += 1;
                if (se == QLatin1String("kotlin"))
                    kt += 1;
            }
        }
    }
    if (kt > 0)
        return QStringLiteral("Kotlin");
    if (java > 0)
        return QStringLiteral("Java");
    return QString();
}

bool detectSpringBoot(const QString &srcDir)
{
    QDir d(srcDir);
    if (!d.exists())
        return false;

    // 递归收集相对路径源码文件(与 macOS 版 subpathsOfDirectory 等价)
    QStringList sources;
    QDirIterator it(srcDir, { QStringLiteral("*.kt"), QStringLiteral("*.java") },
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        sources.append(d.relativeFilePath(it.filePath()));
    }

    // 只扫源码文件;启动类通常叫 *Application,优先检查它,避免大项目全量读取

    std::sort(sources.begin(), sources.end(), [](const QString &a, const QString &b) {
        const bool aApp = QFileInfo(a).fileName().contains(QLatin1String("Application"));
        const bool bApp = QFileInfo(b).fileName().contains(QLatin1String("Application"));
        if (aApp != bApp)
            return aApp;
        return a < b;
    });
    if (sources.size() > 400)
        sources = sources.mid(0, 400);

    for (const QString &f : sources) {
        const QString content = readTextFile(srcDir + QLatin1Char('/') + f);
        if (content.isEmpty())
            continue;
        if (content.contains(QStringLiteral("@SpringBootApplication")))
            return true;
        if (content.contains(QStringLiteral("SpringApplication.run")))
            return true;
    }
    return false;
}

bool detectJib(const QString &dir)
{
    static const QRegularExpression jibBlock(QStringLiteral("\\bjib\\s*\\{"));
    static const QRegularExpression jibId(QStringLiteral("\\bid\\s*\\(?\\s*['\"]jib['\"]"));
    for (const QString &name : { QStringLiteral("build.gradle"), QStringLiteral("build.gradle.kts"), QStringLiteral("pom.xml") }) {
        const QString content = readTextFile(dir + QLatin1Char('/') + name);
        if (content.isEmpty())
            continue;
        if (content.contains(QStringLiteral("com.google.cloud.tools.jib")))
            return true;
        if (content.contains(QStringLiteral("jib-maven-plugin")))
            return true;
        if (jibBlock.match(content).hasMatch())
            return true;
        if (jibId.match(content).hasMatch())
            return true;
    }
    return false;
}

std::optional<Module> makeModule(const QString &dir, const QString &gradlePath,
                                 QString name, BuildTool tool)
{
    QFileInfo dirInfo(dir);
    if (!dirInfo.isDir())
        return std::nullopt;

    const bool hasBuildFile = tool == BuildTool::Gradle
        ? (QFileInfo::exists(dir + QStringLiteral("/build.gradle"))
           || QFileInfo::exists(dir + QStringLiteral("/build.gradle.kts")))
        : QFileInfo::exists(dir + QStringLiteral("/pom.xml"));

    const QString sources = dir + QStringLiteral("/src");
    const QString lang = detectLanguageInDir(sources);
    const bool spring = detectSpringBoot(sources);
    const bool jib = detectJib(dir);
    const auto javaVer = detectModuleJavaVersion(dir);

    // 既没有构建文件也没有源码的目录跳过
    if (!hasBuildFile && lang.isEmpty())
        return std::nullopt;

    Module m;
    m.path = gradlePath;
    m.name = name.isEmpty() ? dirInfo.fileName() : std::move(name);
    m.absolutePath = dir;
    m.language = lang.isEmpty() ? QStringLiteral("Java") : lang;
    m.isSpringBoot = spring;
    m.hasJib = jib;
    m.tool = tool;
    m.javaVersion = javaVer;
    m.availableProfiles = detectProfiles(dir);
    const QString def = detectDefaultProfileInDir(dir);
    if (!def.isEmpty()) {
        m.defaultProfile = def;
        m.hasDefaultProfile = true;
    }
    return m;
}

ModuleList scanGradleModules(const QString &root, const QString &settingsFile)
{
    QStringList modulePaths;

    if (!settingsFile.isEmpty())
        modulePaths = parseGradleIncludes(readTextFile(settingsFile));

    // 没有 include 或解析失败时,扫描一级子目录找 build.gradle
    if (modulePaths.isEmpty()) {
        QDir d(root);
        const QStringList entries = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &e : entries) {
            if (e.startsWith(QLatin1Char('.')))
                continue;
            const QString sub = root + QLatin1Char('/') + e;
            if (QFileInfo::exists(sub + QStringLiteral("/build.gradle"))
                || QFileInfo::exists(sub + QStringLiteral("/build.gradle.kts"))) {
                modulePaths.append(QLatin1Char(':') + e);
            }
        }
    }

    ModuleList modules;
    for (const QString &mp : modulePaths) {
        const QString rel = QString(mp.mid(1)).replace(QLatin1Char(':'), QLatin1Char('/'));
        const QString dir = rel.isEmpty() ? root : root + QLatin1Char('/') + rel;
        auto m = makeModule(dir, mp, QFileInfo(rel).fileName(), BuildTool::Gradle);
        if (m)
            modules.append(*m);
    }
    return modules;
}

/// 递归展开 Maven 聚合模块(packaging=pom 且含 <modules>),只保留叶子模块
void collectMavenModules(const QString &root, const QString &rel, ModuleList &out,
                         int depth, QSet<QString> &visited)
{
    if (depth > 8)
        return;
    const QString dir = rel.isEmpty() ? root : root + QLatin1Char('/') + rel;
    const QString canonical = QFileInfo(dir).canonicalFilePath();
    if (visited.contains(canonical))
        return;
    visited.insert(canonical);

    const QStringList children = parseMavenModules(readTextFile(dir + QStringLiteral("/pom.xml")));
    if (children.isEmpty()) {
        if (!rel.isEmpty()) {
            auto m = makeModule(dir, rel, QFileInfo(rel).fileName(), BuildTool::Maven);
            if (m)
                out.append(*m);
        }
        return;
    }
    for (const QString &child : children) {
        const QString childRel = rel.isEmpty() ? child : rel + QLatin1Char('/') + child;
        collectMavenModules(root, childRel, out, depth + 1, visited);
    }
}

ModuleList scanMavenModules(const QString &root)
{
    ModuleList modules;
    QSet<QString> visited;
    collectMavenModules(root, QString(), modules, 0, visited);
    return modules;
}

int detectJavaVersion(const ModuleList &modules)
{
    // 众数投票
    QMap<int, int> counts;
    for (const Module &m : modules) {
        if (m.javaVersion)
            counts[*m.javaVersion] += 1;
    }
    int best = 0, bestCount = 0;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        if (it.value() > bestCount) {
            bestCount = it.value();
            best = it.key();
        }
    }
    return best;
}

ProjectLanguage detectLanguage(const ModuleList &modules)
{
    const bool hasKotlin = std::any_of(modules.begin(), modules.end(), [](const Module &m) {
        return m.language == QLatin1String("Kotlin");
    });
    const bool hasJava = std::any_of(modules.begin(), modules.end(), [](const Module &m) {
        return m.language == QLatin1String("Java");
    });
    if (hasKotlin && hasJava)
        return ProjectLanguage::Mixed;
    if (hasKotlin)
        return ProjectLanguage::Kotlin;
    return ProjectLanguage::Java;
}

} // namespace

QStringList parseGradleIncludes(const QString &content)
{
    QStringList result;
    QString buffer;
    bool hasBuffer = false;

    const QStringList lines = content.split(QRegularExpression(QStringLiteral("[\\r\\n]")));
    for (const QString &rawLine : lines) {
        const QString codeLine = stripComment(rawLine.trimmed());

        if (!hasBuffer) {
            if (!codeLine.startsWith(QStringLiteral("include")))
                continue;
            // 确认是 include 语句而非 includedBuild 之类的标识符
            const QString rest = codeLine.mid(QStringLiteral("include").size());
            const bool isStatement = rest.isEmpty()
                || rest.startsWith(QLatin1Char('('))
                || rest.startsWith(QLatin1Char(' '))
                || rest.startsWith(QLatin1Char('\''))
                || rest.startsWith(QLatin1Char('"'));
            if (!isStatement)
                continue;
            buffer = codeLine;
            hasBuffer = true;
        } else {
            buffer += QLatin1Char('\n') + codeLine;
        }

        const int openCount = buffer.count(QLatin1Char('('));
        const int closeCount = buffer.count(QLatin1Char(')'));
        if (openCount - closeCount > 0)
            continue;        // 括号未闭合,继续累积

        hasBuffer = false;
        for (const QString &s : quotedStrings(buffer)) {
            const QString normalized = s.startsWith(QLatin1Char(':')) ? s : QLatin1Char(':') + s;
            if (!result.contains(normalized))
                result.append(normalized);
        }
    }

    return result;
}

QStringList parseMavenModules(const QString &content)
{
    QStringList out;
    static const QRegularExpression startTag(QStringLiteral("<modules>"),
                                             QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression endTag(QStringLiteral("</modules>"),
                                           QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression moduleRe(
        QStringLiteral("<module>\\s*([^<]+?)\\s*</module>"),
        QRegularExpression::CaseInsensitiveOption);

    const auto start = startTag.match(content);
    if (!start.hasMatch())
        return out;
    const QString tail = content.mid(start.capturedEnd());
    const auto end = endTag.match(tail);
    if (!end.hasMatch())
        return out;
    const QString block = tail.mid(0, end.capturedStart());

    auto it = moduleRe.globalMatch(block);
    while (it.hasNext()) {
        const auto m = it.next();
        out.append(m.captured(1).trimmed());
    }
    return out;
}

QString detectDefaultProfileInDir(const QString &dir)
{
    const QString resDir = dir + QStringLiteral("/src/main/resources");
    static const QRegularExpression activeRe(QStringLiteral("active\\s*:\\s*([^\\n\\r]+)"));
    for (const QString &name : { QStringLiteral("application.yaml"), QStringLiteral("application.yml"),
                                 QStringLiteral("application.properties") }) {
        const QString content = readTextFile(resDir + QLatin1Char('/') + name);
        if (content.isEmpty())
            continue;
        const auto m = activeRe.match(content);
        if (!m.hasMatch())
            continue;
        QString value = m.captured(0);
        const int colon = value.indexOf(QLatin1Char(':'));
        if (colon >= 0)
            value = value.mid(colon + 1).trimmed();
        // 去掉行尾注释,如 "prod #dev, prod"
        const int hash = value.indexOf(QLatin1Char('#'));
        if (hash >= 0)
            value = value.left(hash);
        // ${SPRING_PROFILES_ACTIVE:prod} → prod
        if (value.contains(QStringLiteral("${"))) {
            const int lastColon = value.lastIndexOf(QLatin1Char(':'));
            if (lastColon >= 0)
                value = value.mid(lastColon + 1);
            value.remove(QLatin1Char('}'));
        }
        value = value.trimmed();
        if (!value.isEmpty())
            value.remove(QRegularExpression(QStringLiteral("[ \"']")));
        if (!value.isEmpty())
            return value;
    }
    return QString();
}

std::optional<int> detectModuleJavaVersion(const QString &dir)
{
    static const QList<QRegularExpression> patterns = {
        QRegularExpression{QStringLiteral("languageVersion\\.set\\(JavaLanguageVersion\\.of\\((\\d+)\\)\\)")},
        QRegularExpression{QStringLiteral("sourceCompatibility\\s*=\\s*JavaVersion\\.VERSION_(\\d+)")},
        QRegularExpression{QStringLiteral("targetCompatibility\\s*=\\s*JavaVersion\\.VERSION_(\\d+)")},
        QRegularExpression{QStringLiteral("<maven\\.compiler\\.source>\\s*(\\d+)")},
        QRegularExpression{QStringLiteral("<java\\.version>\\s*(\\d+)")},
    };
    for (const QString &name : { QStringLiteral("build.gradle"), QStringLiteral("build.gradle.kts"), QStringLiteral("pom.xml") }) {
        const QString content = readTextFile(dir + QLatin1Char('/') + name);
        if (content.isEmpty())
            continue;
        for (const QRegularExpression &re : patterns) {
            const auto m = re.match(content);
            if (m.hasMatch()) {
                bool ok = false;
                const int v = m.captured(1).toInt(&ok);
                if (ok && v > 0)
                    return v;
            }
        }
    }
    return std::nullopt;
}

std::optional<Project> scan(const QString &path)
{
    QFileInfo pathInfo(path);
    if (!pathInfo.isDir())
        return std::nullopt;

    const QString settingsGradle = findFile(path, { QStringLiteral("settings.gradle"), QStringLiteral("settings.gradle.kts") });
    const QString pom = findFile(path, { QStringLiteral("pom.xml") });
    const bool hasBuildGradle = QFileInfo::exists(path + QStringLiteral("/build.gradle"))
        || QFileInfo::exists(path + QStringLiteral("/build.gradle.kts"));

    std::optional<BuildTool> tool;
    if (!settingsGradle.isEmpty() || hasBuildGradle)
        tool = BuildTool::Gradle;
    else if (!pom.isEmpty())
        tool = BuildTool::Maven;
    if (!tool)
        return std::nullopt;

    ModuleList modules;
    switch (*tool) {
    case BuildTool::Gradle: modules = scanGradleModules(path, settingsGradle); break;
    case BuildTool::Maven: modules = scanMavenModules(path); break;
    }

    // 空模块列表时,把根目录本身当作模块
    if (modules.isEmpty()) {
        auto m = makeModule(path, QStringLiteral(":"), pathInfo.fileName(), *tool);
        if (m)
            modules.append(*m);
    }
    if (modules.isEmpty())
        return std::nullopt;

    Project p;
    p.name = pathInfo.fileName();
    p.path = path;
    p.tool = *tool;
    p.language = detectLanguage(modules);
    p.modules = modules;
    p.requiredJava = detectJavaVersion(modules);
    p.hasRequiredJava = p.requiredJava > 0;
    p.hasWrapper = QFileInfo::exists(path + QStringLiteral("/gradlew"));
    return p;
}

} // namespace ProjectScanner
} // namespace jrunner
