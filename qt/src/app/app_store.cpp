#include "app_store.h"

#include "core/debug_log.h"
#include "core/project_cache.h"
#include "core/project_scanner.h"
#include "core/task_planner.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QSet>
#include <QSettings>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

namespace jrunner {

namespace {

constexpr int kScanDepth = 5;
constexpr int kScanBudgetPerRoot = 4000;
constexpr int kLogRetentionDays = 7;

/// 由任务 key 生成稳定的日志文件名(SHA-1 前缀 + 时间戳;避免 NAME_MAX 与 qHash 种子问题)
QString makeLogFilePath(const QString &key)
{
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString dir = AppConfig::logDir();
    const QString base = dir + QStringLiteral("/") + hash + QStringLiteral("-") + stamp;
    QString path = base + QStringLiteral(".jsonl");
    for (int n = 1; QFileInfo::exists(path); ++n)
        path = base + QStringLiteral("-%1.jsonl").arg(n);
    return path;
}

/// 启动时清理超过保留期的日志文件(此时尚无活动任务)
void cleanupOldLogs()
{
    QDir dir(AppConfig::logDir());
    if (!dir.exists())
        return;
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-kLogRetentionDays);
    const QStringList files = dir.entryList({ QStringLiteral("*.jsonl") }, QDir::Files);
    int removed = 0;
    for (const QString &name : files) {
        const QFileInfo info(dir.absoluteFilePath(name));
        if (info.lastModified() < cutoff && QFile::remove(info.absoluteFilePath()))
            ++removed;
    }
    if (removed > 0)
        debugLog(QStringLiteral("log"), QStringLiteral("清理过期日志 %1 个").arg(removed));
}

void sortProjects(QList<Project> &projects)
{
    std::sort(projects.begin(), projects.end(), [](const Project &a, const Project &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
}

} // namespace

AppStore::AppStore(QObject *parent)
    : QObject(parent)
    , m_mcp(this)
{
    m_config = AppConfig::load();
    cleanupOldLogs();
    if (m_config.mcpEnabled)
        m_mcp.start();
    loadScanRoots();
    QSettings settings;
    m_manualProjects = settings.value(QStringLiteral("projects/manual")).toStringList();
    refreshJdks();

    connect(&m_git, &GitMonitor::statusUpdated, this, [this](const QString &) {
        emit projectsChanged();      // 分支徽章随状态刷新
    });
}

AppStore::~AppStore()
{
    stopAll();
}

QString AppStore::taskKey(const QString &projectPath, const QString &modulePath)
{
    return projectPath + QStringLiteral("||") + modulePath;
}

ProcessTask *AppStore::task(const QString &projectPath, const QString &modulePath) const
{
    return m_tasks.value(taskKey(projectPath, modulePath));
}

bool AppStore::isRunning(const QString &projectPath, const QString &modulePath) const
{
    const ProcessTask *t = task(projectPath, modulePath);
    return t && t->state().isActive();
}

bool AppStore::isProjectRunning(const QString &projectPath) const
{
    const QString prefix = projectPath + QStringLiteral("||");
    for (auto it = m_tasks.constBegin(); it != m_tasks.constEnd(); ++it) {
        if (it.key().startsWith(prefix) && it.value()->state().isActive())
            return true;
    }
    return false;
}

std::optional<Project> AppStore::selectedProject() const
{
    return projectAt(m_selectedProjectPath);
}

std::optional<Module> AppStore::selectedModule() const
{
    const auto p = selectedProject();
    if (!p)
        return std::nullopt;
    for (const Module &m : p->modules) {
        if (m.path == m_selectedModulePath)
            return m;
    }
    return std::nullopt;
}

std::optional<Project> AppStore::projectAt(const QString &path) const
{
    for (const Project &p : m_projects) {
        if (p.path == path)
            return p;
    }
    return std::nullopt;
}

void AppStore::loadScanRoots()
{
    QSettings settings;
    if (settings.contains(QStringLiteral("scan/roots"))) {
        m_scanRoots = settings.value(QStringLiteral("scan/roots")).toStringList();
        return;
    }
    const QString home = QDir::homePath();
    const QStringList candidates = {
        QStringLiteral("Documents/code"), QStringLiteral("Documents/codeyd"),
        QStringLiteral("Documents/app"), QStringLiteral("Documents/Projects"),
        QStringLiteral("Documents/workspace"), QStringLiteral("Documents/code-workspace"),
        QStringLiteral("Documents"), QStringLiteral("Projects"),
        QStringLiteral("workspace"), QStringLiteral("IdeaProjects"),
        QStringLiteral("work"), QStringLiteral("code"), QStringLiteral("dev"),
    };
    QStringList roots;
    for (const QString &c : candidates) {
        const QString p = home + QLatin1Char('/') + c;
        if (QFileInfo::exists(p))
            roots.append(p);
    }
    m_scanRoots = roots;
}

void AppStore::setScanRoots(const QStringList &roots)
{
    m_scanRoots = roots;
    QSettings settings;
    settings.setValue(QStringLiteral("scan/roots"), roots);
}

void AppStore::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message)
        return;
    m_statusMessage = message;
    emit statusMessageChanged(message);
}

void AppStore::initializeProjects()
{
    if (m_projectsInitialized)
        return;
    m_projectsInitialized = true;
    const auto snapshot = ProjectCache::load();
    if (!snapshot) {
        scanProjects();
        return;
    }

    m_projects = snapshot->projects;
    applyProjectNames();
    restoreSelection(snapshot->selectedProjectPath.isEmpty() ? m_config.lastOpenedProject
                                                            : snapshot->selectedProjectPath,
                     snapshot->selectedModulePath);
    emit projectsChanged();
    setStatusMessage(m_projects.isEmpty()
                         ? QStringLiteral("未发现项目,点击左下角「添加项目」")
                         : QStringLiteral("已恢复 %1 个项目").arg(m_projects.size()));
    refreshProjectBranches();
}

void AppStore::saveProjectCache() const
{
    if (!ProjectCache::save({m_projects, m_selectedProjectPath, m_selectedModulePath}))
        debugLog(QStringLiteral("cache"), QStringLiteral("无法保存项目缓存: %1").arg(ProjectCache::path()));
}

void AppStore::restoreSelection(QString projectPath, QString modulePath)
{
    auto project = projectAt(projectPath);
    if (!project && !m_projects.isEmpty())
        project = m_projects.first();
    m_selectedProjectPath = project ? project->path : QString();
    m_selectedModulePath = project && !project->modules.isEmpty() ? project->modules.first().path : QString();
    if (project && project->path == projectPath) {
        for (const Module &module : project->modules) {
            if (module.path == modulePath) {
                m_selectedModulePath = modulePath;
                break;
            }
        }
    }
    if (m_config.lastOpenedProject != m_selectedProjectPath) {
        m_config.lastOpenedProject = m_selectedProjectPath;
        m_config.save();
    }
    refreshGit(false);
    emit selectionChanged();
}

void AppStore::refreshProjectBranches()
{
    QStringList paths;
    for (const Project &project : m_projects)
        paths.append(project.path);
    m_git.refreshBranches(paths);
}

QList<Project> AppStore::discoverProjects(const QString &dir, int depth, int &budget)
{
    if (depth <= 0 || budget <= 0)
        return {};
    budget -= 1;
    QList<Project> results;

    if (auto p = ProjectScanner::scan(dir)) {
        results.append(*p);
        // 已是多模块项目,不再深入,避免重复
        if (p->modules.size() > 1)
            return results;
    }

    QDir d(dir);
    const QStringList entries = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    static const QSet<QString> skip = {
        QStringLiteral(".git"), QStringLiteral(".idea"), QStringLiteral("node_modules"),
        QStringLiteral("build"), QStringLiteral("target"), QStringLiteral("out"),
        QStringLiteral(".gradle"), QStringLiteral(".mvn"), QStringLiteral("bin"),
        QStringLiteral("obj"), QStringLiteral("Pods"), QStringLiteral("Library"),
        QStringLiteral("Applications"), QStringLiteral("Music"), QStringLiteral("Movies"),
        QStringLiteral("Pictures"), QStringLiteral(".cache"), QStringLiteral(".venv"),
        QStringLiteral("venv"), QStringLiteral("__pycache__"), QStringLiteral("dist"),
    };
    for (const QString &e : entries) {
        if (e.startsWith(QLatin1Char('.')) || skip.contains(e))
            continue;
        const QString sub = dir + QLatin1Char('/') + e;
        results.append(discoverProjects(sub, depth - 1, budget));
    }
    return results;
}

void AppStore::scanProjects()
{
    if (m_scanning)
        return;
    m_projectsInitialized = true;
    const quint64 generation = m_projectGeneration;
    const QStringList roots = m_scanRoots;
    const QStringList manual = m_manualProjects;
    m_scanning = true;
    emit isScanningChanged();
    setStatusMessage(QStringLiteral("正在扫描..."));

    auto *watcher = new QFutureWatcher<QList<Project>>(this);
    connect(watcher, &QFutureWatcher<QList<Project>>::finished, this, [this, watcher, generation]() {
        QList<Project> found = watcher->result();
        watcher->deleteLater();
        // Keep edits made while scanning, including tombstones for removed paths.
        found.removeIf([&](const Project &p) { return m_projectRevisions.value(p.path) > generation; });
        for (const Project &p : m_projects) {
            if (m_projectRevisions.value(p.path) > generation)
                found.append(p);
        }
        sortProjects(found);
        m_projects = found;
        applyProjectNames();
        restoreSelection(m_selectedProjectPath.isEmpty() ? m_config.lastOpenedProject : m_selectedProjectPath,
                         m_selectedModulePath);
        saveProjectCache();
        m_scanning = false;
        emit isScanningChanged();
        emit projectsChanged();
        debugLog(QStringLiteral("scan"), QStringLiteral("扫描完成,发现 %1 个项目").arg(m_projects.size()));
        setStatusMessage(m_projects.isEmpty()
                             ? QStringLiteral("未发现项目,点击左下角「添加项目」")
                             : QStringLiteral("发现 %1 个项目").arg(m_projects.size()));
        refreshProjectBranches();
    });
    watcher->setFuture(QtConcurrent::run([roots, manual]() {
        debugLog(QStringLiteral("scan"), QStringLiteral("开始扫描 %1 个根目录").arg(roots.size()));
        QList<Project> found;

        // 各根目录并行扫描;所有后台任务只捕获值,不访问 AppStore。
        QList<QFuture<QList<Project>>> perRoot;
        for (const QString &root : roots) {
            perRoot.append(QtConcurrent::run([root]() {
                int budget = kScanBudgetPerRoot;
                return discoverProjects(root, kScanDepth, budget);
            }));
        }
        for (auto &future : perRoot)
            found.append(future.result());

        for (const QString &path : manual) {
            if (std::any_of(found.begin(), found.end(), [&](const Project &p) { return p.path == path; }))
                continue;
            if (auto p = ProjectScanner::scan(path))
                found.append(*p);
        }

        QSet<QString> seen;
        QList<Project> unique;
        for (const Project &p : found) {
            if (seen.contains(p.path))
                continue;
            seen.insert(p.path);
            unique.append(p);
        }
        return unique;
    }));
}

void AppStore::applyProjectNames()
{
    for (Project &p : m_projects)
        p.name = m_config.projectNames.value(p.path, p.name);
}

void AppStore::renameProject(const QString &path, const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        m_config.projectNames.remove(path);
        for (Project &p : m_projects)
            if (p.path == path)
                p.name = QFileInfo(path).fileName();
    } else {
        m_config.projectNames.insert(path, trimmed);
    }
    m_config.save();
    applyProjectNames();
    saveProjectCache();
    emit projectsChanged();
}

void AppStore::addProject(const QString &path)
{
    const QString normalized = QDir::cleanPath(
        path.startsWith(QLatin1Char('~'))
            ? QDir::homePath() + path.mid(1)
            : path);
    const quint64 generation = ++m_projectGeneration;
    m_projectRevisions.insert(normalized, generation);
    setStatusMessage(QStringLiteral("正在识别 %1...").arg(QFileInfo(normalized).fileName()));

    auto *watcher = new QFutureWatcher<std::optional<Project>>(this);
    connect(watcher, &QFutureWatcher<std::optional<Project>>::finished, this,
            [this, watcher, normalized, generation]() {
        const auto scanned = watcher->result();
        watcher->deleteLater();
        if (m_projectRevisions.value(normalized) != generation)
            return;
        if (!scanned) {
            setStatusMessage(QStringLiteral("该目录不是 Gradle / Maven 项目"));
            return;
        }
        const Project p = *scanned;
        if (!projectAt(p.path)) {
            m_projects.append(p);
            applyProjectNames();
            sortProjects(m_projects);
        }
        // Also protect this completed edit from a full scan started after the request.
        m_projectRevisions.insert(p.path, ++m_projectGeneration);
        if (!m_manualProjects.contains(p.path)) {
            m_manualProjects.append(p.path);
            QSettings settings;
            settings.setValue(QStringLiteral("projects/manual"), m_manualProjects);
        }
        selectProject(p.path);
        emit projectsChanged();
        setStatusMessage(QStringLiteral("已添加 %1 · %2 个模块").arg(p.name).arg(p.modules.size()));
    });
    watcher->setFuture(QtConcurrent::run([normalized]() { return ProjectScanner::scan(normalized); }));
}

void AppStore::removeProject(const QString &path)
{
    m_projectRevisions.insert(path, ++m_projectGeneration);
    m_projects.removeIf([&](const Project &p) { return p.path == path; });
    m_manualProjects.removeAll(path);
    QSettings settings;
    settings.setValue(QStringLiteral("projects/manual"), m_manualProjects);
    // 清理该项目的任务
    const QString prefix = path + QStringLiteral("||");
    for (auto it = m_tasks.begin(); it != m_tasks.end();) {
        if (it.key().startsWith(prefix)) {
            it.value()->stop();
            it.value()->deleteLater();
            it = m_tasks.erase(it);
        } else {
            ++it;
        }
    }
    restoreSelection(m_selectedProjectPath, m_selectedModulePath);
    saveProjectCache();
    emit projectsChanged();
}

void AppStore::selectProject(const QString &path)
{
    if (!projectAt(path))
        return;
    restoreSelection(path, path == m_selectedProjectPath ? m_selectedModulePath : QString());
    saveProjectCache();
}

void AppStore::selectModule(const QString &modulePath)
{
    if (m_selectedModulePath == modulePath)
        return;
    const auto project = selectedProject();
    if (!project || std::none_of(project->modules.begin(), project->modules.end(),
                                 [&](const Module &module) { return module.path == modulePath; }))
        return;
    m_selectedModulePath = modulePath;
    saveProjectCache();
    emit selectionChanged();
}

void AppStore::rescanProject(const QString &path)
{
    if (!projectAt(path))
        return;
    const quint64 generation = ++m_projectGeneration;
    m_projectRevisions.insert(path, generation);
    auto *watcher = new QFutureWatcher<std::optional<Project>>(this);
    connect(watcher, &QFutureWatcher<std::optional<Project>>::finished, this, [this, watcher, path, generation]() {
        const auto fresh = watcher->result();
        watcher->deleteLater();
        if (m_projectRevisions.value(path) != generation || !fresh || !projectAt(path))
            return;
        for (Project &project : m_projects) {
            if (project.path == path)
                project = *fresh;
        }
        applyProjectNames();
        m_projectRevisions.insert(path, ++m_projectGeneration);
        if (m_selectedProjectPath == path)
            restoreSelection(path, m_selectedModulePath);
        saveProjectCache();
        emit projectsChanged();
    });
    watcher->setFuture(QtConcurrent::run([path]() { return ProjectScanner::scan(path); }));
}

void AppStore::setActiveTask(const QString &key)
{
    if (m_activeTaskKey == key)
        return;
    m_activeTaskKey = key;
    emit activeTaskChanged();
}

void AppStore::installTask(ProcessTask *task, const QString &key)
{
    if (ProcessTask *previous = m_tasks.value(key))
        previous->deleteLater();
    task->enableFileLog(makeLogFilePath(key));
    m_tasks.insert(key, task);
    connect(task, &ProcessTask::stateChanged, this, [this, key, task]() {
        emit taskStateChanged(key);
        // 终态时刷新左下角状态,避免停留在"正在启动 xxx"
        if (key != m_activeTaskKey)
            return;
        const TaskState state = task->state();
        if (state.kind == TaskState::Success)
            setStatusMessage(QStringLiteral("%1 已结束").arg(task->title()));
        else if (state.kind == TaskState::Failed)
            setStatusMessage(QStringLiteral("%1 失败").arg(task->title()));
    });
    connect(task, &ProcessTask::portDetected, this, [this, key]() {
        emit taskStateChanged(key);
    });
    if (m_activeTaskKey == key)
        emit activeTaskChanged();
}

void AppStore::runModule(const Module &module)
{
    if (const auto project = selectedProject())
        startModule(*project, module);
}

bool AppStore::runModuleAt(const QString &projectPath, const QString &modulePath)
{
    const auto project = projectAt(projectPath);
    if (!project)
        return false;
    for (const Module &m : project->modules) {
        if (m.path == modulePath) {
            startModule(*project, m);
            return true;
        }
    }
    return false;
}

void AppStore::startModule(const Project &project, const Module &module)
{
    const QString key = taskKey(project.path, module.path);

    if (ProcessTask *existing = m_tasks.value(key); existing && existing->state().isActive()) {
        setActiveTask(key);
        return;
    }

    const ModuleConfig cfg = m_config.config(project.path, module.path);
    std::optional<int> wanted;
    if (cfg.javaVersion > 0)
        wanted = cfg.javaVersion;
    else if (module.javaVersion)
        wanted = module.javaVersion;
    else if (project.hasRequiredJava)
        wanted = project.requiredJava;
    const auto jdk = JDKResolver::resolve(wanted);

    auto cmd = TaskPlanner::runCommand(project, module, cfg.profile, jdk, cfg.extraArgs);
    if (!cmd) {
        setStatusMessage(QStringLiteral("无法构造启动命令:未找到 gradlew / mvnw / gradle / mvn"));
        return;
    }
    for (auto it = cfg.envVars.constBegin(); it != cfg.envVars.constEnd(); ++it)
        cmd->environment.insert(it.key(), it.value());

    auto *task = new ProcessTask(*cmd, QStringLiteral("启动 %1").arg(module.name),
                                 cmd->displayCommand, m_config.maxLogLines, this);
    installTask(task, key);
    setActiveTask(key);
    task->start();
    setStatusMessage(QStringLiteral("正在启动 %1...").arg(module.name));
}

void AppStore::buildModule(const Module &module)
{
    if (const auto project = selectedProject())
        buildModuleFor(*project, module);
}

bool AppStore::buildModuleAt(const QString &projectPath, const QString &modulePath)
{
    const auto project = projectAt(projectPath);
    if (!project)
        return false;
    for (const Module &m : project->modules) {
        if (m.path == modulePath) {
            buildModuleFor(*project, m);
            return true;
        }
    }
    return false;
}

void AppStore::buildModuleFor(const Project &project, const Module &module)
{
    const QString key = taskKey(project.path, module.path);

    if (ProcessTask *existing = m_tasks.value(key); existing && existing->state().isActive()) {
        setActiveTask(key);
        return;
    }

    const ModuleConfig cfg = m_config.config(project.path, module.path);
    std::optional<int> wanted;
    if (cfg.javaVersion > 0)
        wanted = cfg.javaVersion;
    else if (module.javaVersion)
        wanted = module.javaVersion;
    else if (project.hasRequiredJava)
        wanted = project.requiredJava;
    const auto jdk = JDKResolver::resolve(wanted);

    auto cmd = TaskPlanner::buildCommand(project, module, cfg.profile, jdk, cfg.extraArgs);
    if (!cmd) {
        setStatusMessage(QStringLiteral("无法构造打包命令:未找到 gradlew / mvnw / gradle / mvn"));
        return;
    }
    for (auto it = cfg.envVars.constBegin(); it != cfg.envVars.constEnd(); ++it)
        cmd->environment.insert(it.key(), it.value());

    auto *task = new ProcessTask(*cmd, QStringLiteral("打包 %1").arg(module.name),
                                 cmd->displayCommand, m_config.maxLogLines, this);
    installTask(task, key);
    setActiveTask(key);
    task->start();
    setStatusMessage(QStringLiteral("正在打包 %1...").arg(module.name));
}

void AppStore::stopModule(const Module &module)
{
    if (const auto project = selectedProject())
        stopModuleFor(*project, module);
}

bool AppStore::stopModuleAt(const QString &projectPath, const QString &modulePath)
{
    const auto project = projectAt(projectPath);
    if (!project)
        return false;
    for (const Module &m : project->modules) {
        if (m.path == modulePath) {
            stopModuleFor(*project, m);
            return true;
        }
    }
    return false;
}

void AppStore::stopModuleFor(const Project &project, const Module &module)
{
    if (ProcessTask *t = task(project.path, module.path))
        t->stop();
}

void AppStore::stopAll()
{
    for (ProcessTask *t : m_tasks) {
        if (t->state().isActive())
            t->stop();
    }
}

void AppStore::updateModuleConfig(const QString &project, const QString &module, const ModuleConfig &cfg)
{
    m_config.setConfig(cfg, project, module);
    m_config.save();
    emit configChanged();
}

QString AppStore::effectiveProfile(const Project &project, const Module &module) const
{
    const ModuleConfig cfg = m_config.config(project.path, module.path);
    if (!cfg.profile.isEmpty())
        return cfg.profile;
    return module.hasDefaultProfile ? module.defaultProfile : QString();
}

void AppStore::setProfile(const QString &profile, const Project &project, const Module &module)
{
    ModuleConfig cfg = m_config.config(project.path, module.path);
    cfg.profile = profile;
    updateModuleConfig(project.path, module.path, cfg);
}

void AppStore::refreshGit(bool force)
{
    if (m_selectedProjectPath.isEmpty())
        return;
    m_git.refresh(m_selectedProjectPath, force);
}

void AppStore::refreshJdks()
{
    auto *watcher = new QFutureWatcher<JDKList>(this);
    connect(watcher, &QFutureWatcher<JDKList>::finished, this, [this, watcher]() {
        m_jdks = watcher->result();
        watcher->deleteLater();
        emit jdksChanged();
    });
    watcher->setFuture(QtConcurrent::run([]() { return JDKResolver::installedJDKs(true); }));
}

void AppStore::runModules(const QString &projectPath, const QStringList &modulePaths)
{
    for (const QString &path : modulePaths)
        runModuleAt(projectPath, path);
}

void AppStore::stopModules(const QString &projectPath, const QStringList &modulePaths)
{
    for (const QString &path : modulePaths)
        stopModuleAt(projectPath, path);
}

void AppStore::buildModules(const QString &projectPath, const QStringList &modulePaths)
{
    for (const QString &path : modulePaths)
        buildModuleAt(projectPath, path);
}

void AppStore::setMcpEnabled(bool enabled)
{
    if (m_config.mcpEnabled == enabled)
        return;
    m_config.mcpEnabled = enabled;
    m_config.save();
    if (enabled)
        m_mcp.start();
    else
        m_mcp.stop();
    emit configChanged();
}

void AppStore::runGroup(const QString &projectPath, const QString &groupName)
{
    const auto groups = m_config.projectGroups(projectPath);
    for (const ModuleGroup &g : groups) {
        if (g.name == groupName) {
            runModules(projectPath, g.modulePaths);
            return;
        }
    }
}

void AppStore::stopGroup(const QString &projectPath, const QString &groupName)
{
    const auto groups = m_config.projectGroups(projectPath);
    for (const ModuleGroup &g : groups) {
        if (g.name == groupName) {
            stopModules(projectPath, g.modulePaths);
            return;
        }
    }
}

void AppStore::updateGroups(const QString &projectPath, const QList<ModuleGroup> &groups)
{
    m_config.setGroups(projectPath, groups);
    m_config.save();
    emit groupsChanged();
}

QList<ModuleGroup> AppStore::projectGroups(const QString &projectPath) const
{
    return m_config.projectGroups(projectPath);
}

} // namespace jrunner
