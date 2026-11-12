#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "core/app_config.h"
#include "core/file_models.h"
#include "core/git_info.h"
#include "core/jdk_resolver.h"
#include "core/models.h"
#include "core/process_task.h"
#include "mcp_control_server.h"

namespace jrunner {

// 【应用状态】对应 macOS 版 AppStore.swift:项目列表、选择状态、任务表、
// 配置与 Git 监控的中央枢纽;信号驱动,所有面板订阅。

class AppStore : public QObject {
    Q_OBJECT

public:
    explicit AppStore(QObject *parent = nullptr);
    ~AppStore() override;

    // ---- 项目 ----
    QList<Project> projects() const { return m_projects; }
    std::optional<Project> selectedProject() const;
    std::optional<Module> selectedModule() const;
    QString selectedProjectPath() const { return m_selectedProjectPath; }
    QString selectedModulePath() const { return m_selectedModulePath; }
    std::optional<Project> projectAt(const QString &path) const;

    QStringList scanRoots() const { return m_scanRoots; }
    void setScanRoots(const QStringList &roots);
    QStringList manualProjects() const { return m_manualProjects; }

    // ---- 任务 ----
    static QString taskKey(const QString &projectPath, const QString &modulePath);
    ProcessTask *task(const QString &projectPath, const QString &modulePath) const;
    ProcessTask *activeTask() const { return m_activeTaskKey.isEmpty() ? nullptr : m_tasks.value(m_activeTaskKey); }
    bool isRunning(const QString &projectPath, const QString &modulePath) const;
    bool isProjectRunning(const QString &projectPath) const;
    QString activeTaskKey() const { return m_activeTaskKey; }

    // ---- 其他 ----
    GitMonitor *git() { return &m_git; }
    EditorModel *editor() { return &m_editor; }
    AppConfig &config() { return m_config; }
    const AppConfig &config() const { return m_config; }
    JDKList jdks() const { return m_jdks; }
    bool isScanning() const { return m_scanning; }
    QString statusMessage() const { return m_statusMessage; }

    // ---- 动作 ----
    void initializeProjects();
    void scanProjects();
    void addProject(const QString &path);
    void removeProject(const QString &path);
    void renameProject(const QString &path, const QString &name);
    void selectProject(const QString &path);
    void selectModule(const QString &modulePath);
    void rescanProject(const QString &path);
    void setActiveTask(const QString &key);
    void setStatusMessage(const QString &message);

    void runModule(const Module &module);
    void buildModule(const Module &module);
    void stopModule(const Module &module);
    void stopAll();
    /// 显式指定项目的启停(不依赖/不改变当前选中),供 MCP 控制使用
    bool runModuleAt(const QString &projectPath, const QString &modulePath);
    bool stopModuleAt(const QString &projectPath, const QString &modulePath);
    bool buildModuleAt(const QString &projectPath, const QString &modulePath);
    void runModules(const QString &projectPath, const QStringList &modulePaths);
    void stopModules(const QString &projectPath, const QStringList &modulePaths);
    void buildModules(const QString &projectPath, const QStringList &modulePaths);
    void runGroup(const QString &projectPath, const QString &groupName);
    void stopGroup(const QString &projectPath, const QString &groupName);
    void updateGroups(const QString &projectPath, const QList<ModuleGroup> &groups);
    QList<ModuleGroup> projectGroups(const QString &projectPath) const;

    void updateModuleConfig(const QString &project, const QString &module, const ModuleConfig &cfg);
    QString effectiveProfile(const Project &project, const Module &module) const;
    void setProfile(const QString &profile, const Project &project, const Module &module);
    void refreshGit(bool force = true);
    void refreshJdks();

    // ---- MCP 控制 ----
    bool mcpEnabled() const { return m_config.mcpEnabled; }
    void setMcpEnabled(bool enabled);
    bool mcpListening() const { return m_mcp.isListening(); }

signals:
    void projectsChanged();
    void selectionChanged();
    void statusMessageChanged(const QString &message);
    void isScanningChanged();
    void taskStateChanged(const QString &key);
    void activeTaskChanged();
    void jdksChanged();
    void configChanged();
    void groupsChanged();

private:
    void loadScanRoots();
    void saveProjectCache() const;
    void applyProjectNames();
    void restoreSelection(QString projectPath, QString modulePath);
    void refreshProjectBranches();
    static QList<Project> discoverProjects(const QString &dir, int depth, int &budget);
    void installTask(ProcessTask *task, const QString &key);
    void startModule(const Project &project, const Module &module);
    void buildModuleFor(const Project &project, const Module &module);
    void stopModuleFor(const Project &project, const Module &module);

    QList<Project> m_projects;
    QString m_selectedProjectPath;
    QString m_selectedModulePath;

    QHash<QString, ProcessTask *> m_tasks;
    QString m_activeTaskKey;

    AppConfig m_config;
    JDKList m_jdks;
    GitMonitor m_git;
    EditorModel m_editor;
    McpControlServer m_mcp;

    QStringList m_scanRoots;
    QStringList m_manualProjects;
    bool m_scanning = false;
    bool m_projectsInitialized = false;
    // Per-path edits (including removals) take precedence over older worker results.
    quint64 m_projectGeneration = 0;
    QHash<QString, quint64> m_projectRevisions;
    QString m_statusMessage;
};

} // namespace jrunner
