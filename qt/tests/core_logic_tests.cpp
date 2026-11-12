// 【核心逻辑回归测试】scanner 解析 / 命令规划 / 日志级别识别 / 端口提取。
#include <QDir>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include "core/models.h"
#include "core/app_config.h"
#include "core/jdk_resolver.h"
#include "core/process_task.h"
#include "core/project_scanner.h"
#include "core/project_cache.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include "core/shell_env.h"
#include "core/task_planner.h"

using namespace jrunner;

namespace {

void writeFile(const QTemporaryDir &dir, const QString &rel, const QByteArray &content)
{
    const QString path = dir.filePath(rel);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
    f.write(content);
}

} // namespace

class CoreLogicTests : public QObject {
    Q_OBJECT

private slots:
    void parseGradleIncludes_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QStringList>("expected");

        QTest::newRow("single line")
            << QStringLiteral("include ':a', ':b'")
            << QStringList{ QStringLiteral(":a"), QStringLiteral(":b") };
        QTest::newRow("multiline")
            << QStringLiteral("include(\n  ':a',\n  ':b'\n)")
            << QStringList{ QStringLiteral(":a"), QStringLiteral(":b") };
        QTest::newRow("comment skipped")
            << QStringLiteral("include ':a' // :ignored\ninclude ':b' # :nope")
            << QStringList{ QStringLiteral(":a"), QStringLiteral(":b") };
        QTest::newRow("quoted unnormalized")
            << QStringLiteral("include 'sub:mod'")
            << QStringList{ QStringLiteral(":sub:mod") };
        QTest::newRow("includedBuild ignored")
            << QStringLiteral("includedBuild('other')\ninclude ':a'")
            << QStringList{ QStringLiteral(":a") };
    }

    void parseGradleIncludes()
    {
        QFETCH(QString, content);
        QFETCH(QStringList, expected);
        QCOMPARE(ProjectScanner::parseGradleIncludes(content), expected);
    }

    void parseMavenModules()
    {
        const QString pom = QStringLiteral(
            "<project><modules><module>api</module><module> web </module></modules></project>");
        const QStringList expected = { QStringLiteral("api"), QStringLiteral("web") };
        QCOMPARE(ProjectScanner::parseMavenModules(pom), expected);
        // 无 modules 块
        QCOMPARE(ProjectScanner::parseMavenModules(QStringLiteral("<project/>")), QStringList());
    }

    void scanGradleProject()
    {
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("settings.gradle"),
                  QByteArray("include ':service-a', ':lib-b'\n"));
        writeFile(dir, QStringLiteral("service-a/build.gradle"),
                  QByteArray("plugins { id 'org.springframework.boot' }\n"));
        writeFile(dir, QStringLiteral("service-a/src/main/java/demo/App.java"),
                  QByteArray("@SpringBootApplication public class App {}\n"));
        writeFile(dir, QStringLiteral("service-a/src/main/resources/application-dev.yaml"),
                  QByteArray("spring:\n  profiles:\n    active: dev\n"));
        writeFile(dir, QStringLiteral("lib-b/build.gradle"),
                  QByteArray("plugins { id 'java' }\n"));

        const auto project = ProjectScanner::scan(dir.path());
        QVERIFY(project.has_value());
        QCOMPARE(project->tool, BuildTool::Gradle);
        QCOMPARE(project->modules.size(), 2);
        const Module *service = nullptr;
        for (const Module &m : project->modules) {
            if (m.path == QLatin1String(":service-a"))
                service = &m;
        }
        QVERIFY(service);
        QVERIFY(service->isSpringBoot);
        QCOMPARE(service->language, QStringLiteral("Java"));
        QVERIFY(service->availableProfiles.contains(QStringLiteral("dev")));
    }

    void detectDefaultProfile()
    {
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("src/main/resources/application.yaml"),
                  QByteArray("spring:\n  profiles:\n    active: ${SPRING_PROFILES_ACTIVE:prod}\n"));
        QCOMPARE(ProjectScanner::detectDefaultProfileInDir(dir.path()), QStringLiteral("prod"));
    }

    void detectModuleJavaVersion()
    {
        QTemporaryDir dir;
        QFile f(dir.filePath(QStringLiteral("build.gradle.kts")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("java { toolchain { languageVersion.set(JavaLanguageVersion.of(21)) } }\n");
        f.close();
        QCOMPARE(ProjectScanner::detectModuleJavaVersion(dir.path()).value_or(0), 21);
    }

    void logLevelDetection()
    {
        QCOMPARE(detectLogLevel(QStringLiteral("2024 ERROR something")), LogLevel::Error);
        QCOMPARE(detectLogLevel(QStringLiteral("WARN: deprecated")), LogLevel::Warn);
        QCOMPARE(detectLogLevel(QStringLiteral("INFO started")), LogLevel::Info);
        QCOMPARE(detectLogLevel(QStringLiteral("DEBUG trace")), LogLevel::Debug);
        QCOMPARE(detectLogLevel(QStringLiteral("plain text")), LogLevel::Plain);
    }

    void extractPort()
    {
        QCOMPARE(ProcessTask::extractPort(
                     QStringLiteral("Tomcat started on port(s): 8080 (http)")),
                 8080);
        QCOMPARE(ProcessTask::extractPort(QStringLiteral("Netty started on port 9090.")), 9090);
        QCOMPARE(ProcessTask::extractPort(QStringLiteral("no ports here")), 0);
    }

    void stripAnsi()
    {
        QCOMPARE(ProcessTask::stripAnsi(QStringLiteral("\u001B[32mGREEN\u001B[0m")),
                 QStringLiteral("GREEN"));
    }

    void taskPlannerQuote()
    {
        QCOMPARE(TaskPlanner::quoteIfNeeded(QStringLiteral("plain")), QStringLiteral("plain"));
        QCOMPARE(TaskPlanner::quoteIfNeeded(QStringLiteral("with space")),
                 QStringLiteral("\"with space\""));
    }

    void taskPlannerGradleCommand()
    {
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("settings.gradle"), QByteArray("include ':a'\n"));
        writeFile(dir, QStringLiteral("a/build.gradle"), QByteArray("plugins { id 'java' }\n"));

        const auto project = ProjectScanner::scan(dir.path());
        QVERIFY(project.has_value());
        QVERIFY(!project->modules.isEmpty());
        const Module &m = project->modules.first();

        // 无 wrapper、无系统 gradle 时优雅失败;有则校验参数
        const auto cmd = TaskPlanner::runCommand(*project, m, QStringLiteral("dev"), std::nullopt);
        if (cmd.has_value()) {
            QVERIFY(cmd->arguments.contains(QStringLiteral("--console=plain")));
            QVERIFY(cmd->arguments.contains(QStringLiteral(":a:bootRun")));
            QVERIFY(cmd->arguments.contains(
                QStringLiteral("--args=--spring.profiles.active=dev")));
            QCOMPARE(cmd->workingDirectory, project->path);
        } else {
            QVERIFY(Shell::which(QStringLiteral("gradle")).isEmpty());
        }
    }
    void projectCacheRoundTrip()
    {
        QTemporaryDir dir;
        Project project;
        project.name = QStringLiteral("缓存项目");
        project.path = dir.path();
        project.tool = BuildTool::Maven;
        project.language = ProjectLanguage::Mixed;
        project.requiredJava = 21;
        project.hasRequiredJava = true;
        project.hasWrapper = true;
        Module module;
        module.path = QStringLiteral("service");
        module.name = QStringLiteral("服务");
        module.absolutePath = dir.filePath(QStringLiteral("service"));
        module.language = QStringLiteral("Kotlin");
        module.tool = BuildTool::Maven;
        module.isSpringBoot = true;
        module.hasJib = true;
        module.javaVersion = 21;
        module.availableProfiles = {QStringLiteral("dev"), QStringLiteral("prod")};
        module.defaultProfile = QStringLiteral("dev");
        module.hasDefaultProfile = true;
        project.modules.append(module);
        const QString cache = dir.filePath(QStringLiteral("projects.json"));
        QVERIFY(ProjectCache::save({{project}, project.path, module.path}, cache));
        const auto restored = ProjectCache::load(cache);
        QVERIFY(restored);
        QCOMPARE(restored->projects.size(), 1);
        QCOMPARE(restored->projects.first().toJson(), project.toJson());
        QCOMPARE(restored->selectedProjectPath, project.path);
        QCOMPARE(restored->selectedModulePath, module.path);
        QVERIFY(ProjectCache::save({}, cache));
        QVERIFY(ProjectCache::load(cache));
        QVERIFY(ProjectCache::load(cache)->projects.isEmpty());
        writeFile(dir, QStringLiteral("projects.json"), QByteArray("{\"version\":999,\"projects\":[]}"));
        QVERIFY(!ProjectCache::load(cache));
        writeFile(dir, QStringLiteral("projects.json"), QByteArray("{broken"));
        QVERIFY(!ProjectCache::load(cache));
    }

    void mavenSiblingDependencyPlan()
    {
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("mvnw"), QByteArray("#!/bin/sh\nexit 0\n"));
        QVERIFY(QFile::setPermissions(dir.filePath(QStringLiteral("mvnw")),
                                     QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        Project project;
        project.path = dir.path();
        project.tool = BuildTool::Maven;
        Module module;
        module.path = QStringLiteral("services/api");
        module.tool = BuildTool::Maven;
        module.isSpringBoot = true;
        const QStringList extra{QStringLiteral("-s"), QStringLiteral("/tmp/settings with spaces.xml"),
                                QStringLiteral("-Pdevelopment"), QStringLiteral("-Dcustom=value with spaces")};
        const auto command = TaskPlanner::runCommand(project, module, QStringLiteral("dev"), std::nullopt, extra);
        QVERIFY(command);
        QCOMPARE(command->executable, dir.filePath(QStringLiteral("mvnw")));
        QCOMPARE(command->workingDirectory, dir.path());
        QVERIFY(command->preparationArguments.contains(QStringLiteral("install")));
        QVERIFY(command->preparationArguments.contains(QStringLiteral("-am")));
        QCOMPARE(command->preparationArguments.value(command->preparationArguments.indexOf(QStringLiteral("-pl")) + 1), module.path);
        QVERIFY(!command->preparationArguments.contains(QStringLiteral("spring-boot:run")));
        QVERIFY(command->arguments.contains(QStringLiteral("spring-boot:run")));
        QVERIFY(!command->arguments.contains(QStringLiteral("-am")));
        QVERIFY(command->arguments.contains(QStringLiteral("-Dspring-boot.run.profiles=dev")));
        QVERIFY(!command->arguments.contains(QStringLiteral("-P")));
        for (const QString &arg : extra) {
            QVERIFY(command->arguments.contains(arg));
            QVERIFY(command->preparationArguments.contains(arg));
        }
        QCOMPARE(command->arguments.count(QStringLiteral("-s")), 1);
        QVERIFY(command->displayCommand.contains(QStringLiteral("settings with spaces.xml")));
        module.path = QStringLiteral(":");
        const auto root = TaskPlanner::runCommand(project, module, {}, std::nullopt, extra);
        QVERIFY(root);
        QVERIFY(root->preparationArguments.isEmpty());
        QVERIFY(!root->arguments.contains(QStringLiteral("-pl")));
        const auto build = TaskPlanner::buildCommand(project, module, {}, std::nullopt, extra);
        QVERIFY(build);
        QVERIFY(build->arguments.contains(QStringLiteral("package")));
        QVERIFY(!build->arguments.contains(QStringLiteral("jib:build")));
    }

    void preparationStages_data()
    {
        QTest::addColumn<int>("prepareExit");
        QTest::newRow("success") << 0;
        QTest::newRow("failure") << 7;
    }

    void preparationStages()
    {
        QFETCH(int, prepareExit);
        PlannedCommand command;
        command.executable = QStringLiteral("/bin/sh");
        command.preparationArguments = {QStringLiteral("-c"),
            QStringLiteral("printf 'PREPARED\\n'; exit %1").arg(prepareExit)};
        command.arguments = {QStringLiteral("-c"), QStringLiteral("printf 'RUNNING\\n'")};
        command.refreshDisplayCommand();
        ProcessTask task(command, QStringLiteral("test"), QString());
        task.start();
        QTRY_VERIFY(!task.state().isActive());
        QCOMPARE(task.state().kind, prepareExit == 0 ? TaskState::Success : TaskState::Failed);
        bool ran = false;
        for (const LogLine &line : task.lines())
            ran |= line.text == QLatin1String("RUNNING");
        QCOMPARE(ran, prepareExit == 0);
    }

    void cancellationStopsPreparation()
    {
        PlannedCommand command;
        command.executable = QStringLiteral("/bin/sh");
        command.preparationArguments = {QStringLiteral("-c"), QStringLiteral("printf 'READY\\n'; exec sleep 30")};
        command.arguments = {QStringLiteral("-c"), QStringLiteral("printf 'MUST_NOT_RUN\\n'")};
        ProcessTask task(command, QStringLiteral("test"), QString());
        task.start();
        QTRY_VERIFY(std::any_of(task.lines().begin(), task.lines().end(), [](const LogLine &line) {
            return line.text == QLatin1String("READY");
        }));
        QCOMPARE(task.state().kind, TaskState::Starting);
        task.stop();
        QTRY_VERIFY(!task.state().isActive());
        QCOMPARE(task.state().kind, TaskState::Success);
        QVERIFY(std::none_of(task.lines().begin(), task.lines().end(), [](const LogLine &line) {
            return line.text == QLatin1String("MUST_NOT_RUN");
        }));
    }

    void fileLogPersistsBeyondWindow()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        PlannedCommand command;
        command.executable = QStringLiteral("/bin/sh");
        command.arguments = { QStringLiteral("-c"),
            QStringLiteral("for i in $(seq 1 120); do printf 'line-%03d\\n' \"$i\"; done") };
        command.refreshDisplayCommand();
        ProcessTask task(command, QStringLiteral("test"), QString(), 10);
        task.enableFileLog(dir.filePath(QStringLiteral("history.jsonl")));
        task.start();
        QTRY_VERIFY(!task.state().isActive());
        QCOMPARE(task.state().kind, TaskState::Success);
        // 内存窗口截断到 10,磁盘保留全部($ cmd + 120 行 + 成功行 = 122)
        QCOMPARE(task.lines().size(), 10);
        QCOMPARE(task.totalLineCount(), quint64(122));
        const QList<LogLine> early = task.readHistory(1, 3);
        QCOMPARE(early.size(), 3);
        QCOMPARE(early.at(0).text, QStringLiteral("line-001"));
        QCOMPARE(early.at(1).text, QStringLiteral("line-002"));
        QCOMPARE(early.at(2).text, QStringLiteral("line-003"));
        QCOMPARE(task.searchHistory(QStringLiteral("line-001"), 0, 10).size(), 1);
        const QString exported = dir.filePath(QStringLiteral("export.log"));
        QVERIFY(ProcessTask::exportHistoryToFile(task.logFilePath(), exported));
        QFile f(exported);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(f.readAll());
        QVERIFY(text.contains(QStringLiteral("line-001")));
        QVERIFY(text.contains(QStringLiteral("line-120")));
        QVERIFY(text.contains(QStringLiteral("[INFO]")));
    }

    void clearLogsResetsFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        PlannedCommand command;
        command.executable = QStringLiteral("/bin/sh");
        command.arguments = { QStringLiteral("-c"),
            QStringLiteral("printf 'AAA\\n'; sleep 0.4; printf 'BBB\\n'") };
        command.refreshDisplayCommand();
        ProcessTask task(command, QStringLiteral("test"), QString(), 100);
        task.enableFileLog(dir.filePath(QStringLiteral("clear.jsonl")));
        task.start();
        QTRY_VERIFY(std::any_of(task.lines().begin(), task.lines().end(), [](const LogLine &l) {
            return l.text == QLatin1String("AAA");
        }));
        task.clearLogs();
        QCOMPARE(task.totalLineCount(), quint64(0));
        QVERIFY(task.lines().isEmpty());
        QTRY_VERIFY(!task.state().isActive());
        const QList<LogLine> all = task.readHistory(0, 100);
        bool hasA = false, hasB = false;
        for (const LogLine &l : all) {
            hasA |= l.text == QLatin1String("AAA");
            hasB |= l.text == QLatin1String("BBB");
        }
        QVERIFY(!hasA);
        QVERIFY(hasB);
    }

    void searchHistoryLevelFilter()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        PlannedCommand command;
        command.executable = QStringLiteral("/bin/sh");
        command.arguments = { QStringLiteral("-c"),
            QStringLiteral("printf 'ERROR needle-one\\nWARN needle-two\\nINFO needle-three\\n'") };
        command.refreshDisplayCommand();
        ProcessTask task(command, QStringLiteral("test"), QString(), 100);
        task.enableFileLog(dir.filePath(QStringLiteral("levels.jsonl")));
        task.start();
        QTRY_VERIFY(!task.state().isActive());
        // 命令回显是 Debug 级别,会被 mode 1/2 排除
        const QList<LogLine> errors = task.searchHistory(QStringLiteral("needle"), 1, 100);
        QCOMPARE(errors.size(), 1);
        QCOMPARE(errors.at(0).text, QStringLiteral("ERROR needle-one"));
        QCOMPARE(task.searchHistory(QStringLiteral("needle"), 2, 100).size(), 2);
        const QList<LogLine> all = task.searchHistory(QStringLiteral("needle-three"), 0, 100);
        QVERIFY(std::any_of(all.begin(), all.end(), [](const LogLine &l) {
            return l.text == QLatin1String("INFO needle-three");
        }));
    }

    void partialTrailingRecordIgnored()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        PlannedCommand command;
        command.executable = QStringLiteral("/bin/sh");
        command.arguments = { QStringLiteral("-c"), QStringLiteral("printf 'COMPLETE\\n'") };
        command.refreshDisplayCommand();
        ProcessTask task(command, QStringLiteral("test"), QString(), 100);
        const QString logPath = dir.filePath(QStringLiteral("partial.jsonl"));
        task.enableFileLog(logPath);
        task.start();
        QTRY_VERIFY(!task.state().isActive());
        const quint64 before = task.totalLineCount();
        QVERIFY(before >= 2);
        QFile f(logPath);
        QVERIFY(f.open(QIODevice::Append));
        f.write("{\"t\":1,\"l\":2,\"x\":\"broken");      // 无换行的撕裂半行
        f.close();
        const QList<LogLine> all = task.readHistory(0, 100);
        QCOMPARE(quint64(all.size()), before);
        QVERIFY(std::any_of(all.begin(), all.end(), [](const LogLine &l) {
            return l.text == QLatin1String("COMPLETE");
        }));
        QVERIFY(task.searchHistory(QStringLiteral("broken"), 0, 100).isEmpty());
    }

    void appConfigMcpFlagRoundTrip()
    {
        AppConfig cfg;
        QVERIFY(!cfg.mcpEnabled);
        cfg.mcpEnabled = true;
        QVERIFY(AppConfig::fromJson(cfg.toJson()).mcpEnabled);
        QVERIFY(!AppConfig::fromJson(QJsonObject{}).mcpEnabled);
    }

    void nestedMavenModulesAreExpanded()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(dir, QStringLiteral("pom.xml"),
                  "<project><modules><module>agg</module><module>plain</module></modules></project>");
        writeFile(dir, QStringLiteral("agg/pom.xml"),
                  "<project><packaging>pom</packaging><modules><module>leaf</module></modules></project>");
        writeFile(dir, QStringLiteral("agg/leaf/pom.xml"), "<project><artifactId>leaf</artifactId></project>");
        writeFile(dir, QStringLiteral("plain/pom.xml"), "<project><artifactId>plain</artifactId></project>");
        const auto project = ProjectScanner::scan(dir.path());
        QVERIFY(project);
        QStringList paths;
        for (const Module &m : project->modules)
            paths.append(m.path);
        QVERIFY(paths.contains(QStringLiteral("agg/leaf")));
        QVERIFY(paths.contains(QStringLiteral("plain")));
        QVERIFY(!paths.contains(QStringLiteral("agg")));   // 聚合模块本身不作为可运行模块
    }

    void jdkListHasNoDuplicates()
    {
        const JDKList jdks = JDKResolver::installedJDKs(true);
        QSet<QString> homes;
        QSet<QString> identities;
        for (const JDK &j : jdks) {
            QVERIFY(!homes.contains(j.home));
            homes.insert(j.home);
            const QString id = QStringLiteral("%1|%2|%3").arg(j.version).arg(j.fullVersion, j.vendor);
            QVERIFY(!identities.contains(id));
            identities.insert(id);
        }
    }
};

QTEST_MAIN(CoreLogicTests)
#include "core_logic_tests.moc"
