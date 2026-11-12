#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace jrunner {

// 【全局配置】对应 macOS 版 AppConfig.swift,JSON 持久化到 ~/.config/jrunner/config.json。

/// 单个模块的运行配置
struct ModuleConfig {
    /// 启动时使用的 Spring profile(空 = 跟随项目配置)
    QString profile;
    /// 覆盖 JDK 版本(0 = 跟随项目)
    int javaVersion = 0;
    /// 额外环境变量
    QHash<QString, QString> envVars;
    /// 额外构建参数
    QStringList extraArgs;

    bool operator==(const ModuleConfig &) const = default;
};

/// 模块分组
struct ModuleGroup {
    QString name;
    QStringList modulePaths;
    bool operator==(const ModuleGroup &) const = default;
};

/// 全局配置
struct AppConfig {
    /// projectPath -> modulePath -> config
    QHash<QString, QHash<QString, ModuleConfig>> projects;
    QString lastOpenedProject;
    QString logLevelFilter = QStringLiteral("info");
    int maxLogLines = 20000;
    /// 是否启用 MCP 控制服务(允许外部 agent 启停模块)
    bool mcpEnabled = false;
    /// 项目自定义显示名:projectPath -> name(空/缺失 = 目录名)
    QHash<QString, QString> projectNames;
    QHash<QString, QList<ModuleGroup>> groups;

    static AppConfig load();
    void save() const;

    static QString configDir();
    static QString configPath();
    static QString logDir();

    ModuleConfig config(const QString &project, const QString &module) const;
    void setConfig(const ModuleConfig &cfg, const QString &project, const QString &module);
    QList<ModuleGroup> projectGroups(const QString &project) const;
    void setGroups(const QString &project, const QList<ModuleGroup> &groups);

    QJsonObject toJson() const;
    static AppConfig fromJson(const QJsonObject &root);
};

} // namespace jrunner
