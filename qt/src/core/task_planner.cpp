#include "task_planner.h"

#include "debug_log.h"
#include "shell_env.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

namespace jrunner {
namespace TaskPlanner {

namespace {

/// 读取 gradle-wrapper.properties 里声明的 gradle 版本
std::optional<QList<int>> wrapperDeclaredVersion(const QString &root)
{
    QFile file(root + QStringLiteral("/gradle/wrapper/gradle-wrapper.properties"));
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QString content = QString::fromUtf8(file.readAll());

    static const QRegularExpression re(QStringLiteral("gradle-(\\d+(?:\\.\\d+)*)-(?:bin|all)\\.zip"));
    const auto m = re.match(content);
    if (!m.hasMatch())
        return std::nullopt;
    const QString verStr = m.captured(1);
    QList<int> nums;
    for (const QString &part : verStr.split(QLatin1Char('.'))) {
        bool ok = false;
        const int v = part.toInt(&ok);
        if (ok)
            nums.append(v);
    }
    if (nums.isEmpty())
        return std::nullopt;
    return nums;
}

/// 比较版本序列:a < b 返回 -1,a > b 返回 1,相等返回 0
int compareVersions(const QList<int> &a, const QList<int> &b)
{
    const int n = qMin(a.size(), b.size());
    for (int i = 0; i < n; ++i) {
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    }
    if (a.size() == b.size())
        return 0;
    return a.size() < b.size() ? -1 : 1;
}

/// 从 ~/.gradle/wrapper/dists 里找已缓存的 gradle 发行版(离线兜底)。
/// 优先匹配项目声明的版本,避免版本不兼容导致构建失败。
QString cachedGradleBinary(const std::optional<QList<int>> &preferred)
{
    const QString base = QDir::homePath() + QStringLiteral("/.gradle/wrapper/dists");
    QDir distsDir(base);
    if (!distsDir.exists())
        return QString();

    struct Candidate {
        QList<int> version;
        QString path;
    };
    QList<Candidate> candidates;

    const QStringList dists = distsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &dist : dists) {
        QDir distPath(base + QLatin1Char('/') + dist);
        const QStringList hashes = distPath.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &hash : hashes) {
            QDir hashPath(distPath.absoluteFilePath(hash));
            const QStringList versions = hashPath.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString &v : versions) {
                if (!v.startsWith(QStringLiteral("gradle-")))
                    continue;
                const QString bin = hashPath.absoluteFilePath(v) + QStringLiteral("/bin/gradle");
                QFileInfo binInfo(bin);
                if (!binInfo.isExecutable() || !binInfo.isFile())
                    continue;
                const QStringList segments = v.mid(QStringLiteral("gradle-").size()).split(QLatin1Char('-'));
                QList<int> nums;
                for (const QString &part : segments.value(0).split(QLatin1Char('.'))) {
                    bool ok = false;
                    const int n = part.toInt(&ok);
                    if (ok)
                        nums.append(n);
                }
                if (nums.isEmpty())
                    continue;
                candidates.append({ nums, bin });
            }
        }
    }
    if (candidates.isEmpty())
        return QString();

    if (!preferred) {
        // 无声明版本:取最高
        const Candidate *best = &candidates.first();
        for (const Candidate &c : candidates) {
            if (compareVersions(c.version, best->version) > 0)
                best = &c;
        }
        return best->path;
    }

    // 1. 精确匹配
    for (const Candidate &c : candidates) {
        if (compareVersions(c.version, *preferred) == 0)
            return c.path;
    }
    // 2. 高于声明版本中最低的(向后兼容性更好)
    const Candidate *higher = nullptr;
    for (const Candidate &c : candidates) {
        if (compareVersions(c.version, *preferred) > 0
            && (!higher || compareVersions(c.version, higher->version) < 0))
            higher = &c;
    }
    if (higher)
        return higher->path;
    // 3. 否则取低于声明版本中最高的
    const Candidate *lower = nullptr;
    for (const Candidate &c : candidates) {
        if (compareVersions(c.version, *preferred) < 0
            && (!lower || compareVersions(c.version, lower->version) > 0))
            lower = &c;
    }
    return lower ? lower->path : QString();
}

/// 构造 gradle 任务路径。
/// 根模块(路径为 ":" 或空)直接用 task;子模块用 :path:task
QString taskPathFor(const Module &module, const QString &task)
{
    const QString p = module.path.trimmed();
    if (p.isEmpty() || p == QLatin1String(":") || p == QLatin1String("::"))
        return task;
    const QString normalized = p.startsWith(QLatin1Char(':')) ? p : QLatin1Char(':') + p;
    return normalized + QLatin1Char(':') + task;
}

bool isExecutableFile(const QString &path)
{
    const QFileInfo info(path);
    return info.isFile() && info.isExecutable();
}

std::optional<PlannedCommand> gradleCommand(const Project &project, const Module &module,
                                            const QString &task, const QString &profile,
                                            const std::optional<JDK> &jdk)
{
    const QString root = project.path;
    const QString wrapper = root + QStringLiteral("/gradlew");

    QString executable;
    if (isExecutableFile(wrapper)) {
        executable = wrapper;
    } else if (const QString systemGradle = Shell::which(QStringLiteral("gradle")); !systemGradle.isEmpty()) {
        executable = systemGradle;
    } else if (const QString cached = cachedGradleBinary(wrapperDeclaredVersion(root)); !cached.isEmpty()) {
        executable = cached;
    } else {
        return std::nullopt;
    }

    QStringList args;
    // 非交互输出:纯文本日志,避免 ANSI 进度条污染
    args.append(QStringLiteral("--console=plain"));
    args.append(taskPathFor(module, task));

    // Spring Boot 的 profiles 参数
    if (!profile.isEmpty() && task == QLatin1String("bootRun"))
        args.append(QStringLiteral("--args=--spring.profiles.active=") + profile);

    // 继承用户 shell 环境(数据库地址、Nacos 等变量通常在这里)
    QHash<QString, QString> env = Shell::userEnvironment();
    if (jdk) {
        env.insert(QStringLiteral("JAVA_HOME"), jdk->home);
        env.insert(QStringLiteral("PATH"), jdk->home + QStringLiteral("/bin:") + Shell::userPath());
    } else {
        env.insert(QStringLiteral("PATH"), Shell::userPath());
    }
    // Gradle daemon 内存
    if (!env.contains(QStringLiteral("GRADLE_OPTS")))
        env.insert(QStringLiteral("GRADLE_OPTS"), QStringLiteral("-Xmx2g"));

    PlannedCommand cmd;
    cmd.executable = executable;
    cmd.arguments = args;
    cmd.workingDirectory = root;
    cmd.environment = env;
    cmd.displayCommand = (isExecutableFile(wrapper) ? QStringLiteral("./gradlew ")
                                                    : QStringLiteral("gradle "))
        + taskPathFor(module, task)
        + (!profile.isEmpty() && task == QLatin1String("bootRun")
               ? QStringLiteral(" --args=--spring.profiles.active=") + profile
               : QString());
    return cmd;
}

std::optional<PlannedCommand> mavenCommand(const Project &project, const Module &module,
                                           const QString &goal, const QString &profile,
                                           const std::optional<JDK> &jdk,
                                           const QStringList &extraArgs)
{
    const QString root = project.path;
    const QString wrapper = root + QStringLiteral("/mvnw");

    QString executable;
    if (isExecutableFile(wrapper)) {
        executable = wrapper;
    } else if (const QString systemMvn = Shell::which(QStringLiteral("mvn")); !systemMvn.isEmpty()) {
        executable = systemMvn;
    } else {
        return std::nullopt;
    }

    QStringList args{QStringLiteral("-B")};
    const bool hasSettings = std::any_of(extraArgs.begin(), extraArgs.end(), [](const QString &arg) {
        return arg == QLatin1String("--settings") || arg.startsWith(QLatin1String("--settings="))
            || arg.startsWith(QLatin1String("-s"));
    });
    const QString settings = QDir::homePath() + QStringLiteral("/.m2/settings-unicom.xml");
    if (!hasSettings && QFileInfo::exists(settings))
        args.append({QStringLiteral("-s"), settings});
    const bool childModule = !module.path.isEmpty() && module.path != QLatin1String(".")
        && module.path != QLatin1String(":");
    if (childModule)
        args.append({QStringLiteral("-pl"), module.path});

    QStringList preparation;
    if (childModule && goal == QLatin1String("spring-boot:run")) {
        preparation = args;
        preparation.append({QStringLiteral("-am"), QStringLiteral("install"),
                            QStringLiteral("-DskipTests"),
                            QStringLiteral("-Dspring-boot.repackage.skip=true")});
        preparation.append(extraArgs);
    }
    args.append(goal);
    if (childModule && goal == QLatin1String("package"))
        args.append(QStringLiteral("-am"));
    if (!profile.isEmpty() && goal == QLatin1String("spring-boot:run"))
        args.append(QStringLiteral("-Dspring-boot.run.profiles=") + profile);
    args.append(extraArgs);

    QHash<QString, QString> env = Shell::userEnvironment();
    if (jdk) {
        env.insert(QStringLiteral("JAVA_HOME"), jdk->home);
        env.insert(QStringLiteral("PATH"), jdk->home + QStringLiteral("/bin:") + Shell::userPath());
    } else {
        env.insert(QStringLiteral("PATH"), Shell::userPath());
    }

    PlannedCommand cmd;
    cmd.executable = executable;
    cmd.arguments = args;
    cmd.workingDirectory = root;
    cmd.environment = env;
    cmd.preparationArguments = preparation;
    cmd.refreshDisplayCommand();
    return cmd;
}

} // namespace

std::optional<PlannedCommand> runCommand(const Project &project, const Module &module,
                                         const QString &profile, const std::optional<JDK> &jdk,
                                         const QStringList &extraArgs)
{
    if (module.tool == BuildTool::Maven)
        return mavenCommand(project, module, QStringLiteral("spring-boot:run"), profile, jdk, extraArgs);
    auto command = gradleCommand(project, module, QStringLiteral("bootRun"), profile, jdk);
    if (command) {
        command->arguments.append(extraArgs);
        command->refreshDisplayCommand();
    }
    return command;
}

std::optional<PlannedCommand> buildCommand(const Project &project, const Module &module,
                                           const QString &profile, const std::optional<JDK> &jdk,
                                           const QStringList &extraArgs)
{
    if (module.tool == BuildTool::Maven)
        return mavenCommand(project, module,
                            module.hasJib ? QStringLiteral("jib:build") : QStringLiteral("package"),
                            profile, jdk, extraArgs);
    auto command = gradleCommand(project, module,
                                 module.hasJib ? QStringLiteral("jib") : QStringLiteral("build"),
                                 profile, jdk);
    if (command) {
        command->arguments.append(extraArgs);
        command->refreshDisplayCommand();
    }
    return command;
}

QStringList availableRunTasks(const Module &module)
{
    QStringList tasks;
    switch (module.tool) {
    case BuildTool::Gradle:
        tasks.append(QStringLiteral("bootRun"));
        if (module.hasJib)
            tasks.append(QStringLiteral("jib"));
        break;
    case BuildTool::Maven:
        tasks.append(QStringLiteral("spring-boot:run"));
        if (module.hasJib)
            tasks.append(QStringLiteral("jib:build"));
        break;
    }
    return tasks;
}

QString quoteIfNeeded(const QString &s)
{
    if (s.contains(QLatin1Char(' ')) || s.contains(QLatin1Char('"')) || s.contains(QLatin1Char('\''))) {
        QString escaped = s;
        escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        return QLatin1Char('"') + escaped + QLatin1Char('"');
    }
    return s;
}

} // namespace TaskPlanner

QString PlannedCommand::displayForArguments(const QStringList &args) const
{
    QStringList parts{TaskPlanner::quoteIfNeeded(executable)};
    for (const QString &arg : args)
        parts.append(TaskPlanner::quoteIfNeeded(arg));
    return parts.join(QLatin1Char(' '));
}

void PlannedCommand::refreshDisplayCommand()
{
    displayCommand = displayForArguments(arguments);
    if (!preparationArguments.isEmpty())
        displayCommand = displayForArguments(preparationArguments) + QStringLiteral(" && ") + displayCommand;
}

} // namespace jrunner
