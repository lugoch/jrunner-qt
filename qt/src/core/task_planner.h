#pragma once

#include "jdk_resolver.h"
#include "models.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <optional>

namespace jrunner {

// 【任务规划】把模块转换为具体的运行/打包命令,对应 macOS 版 TaskPlanner.swift。

/// 一条可执行命令
struct PlannedCommand {
    QString executable;
    QStringList arguments;
    QString workingDirectory;
    QHash<QString, QString> environment;
    QString displayCommand;
    QStringList preparationArguments;

    QString displayForArguments(const QStringList &args) const;
    void refreshDisplayCommand();
};

namespace TaskPlanner {

/// 生成启动命令
std::optional<PlannedCommand> runCommand(const Project &project, const Module &module,
                                         const QString &profile, const std::optional<JDK> &jdk,
                                         const QStringList &extraArgs = {});

/// 生成打包命令(jib)
std::optional<PlannedCommand> buildCommand(const Project &project, const Module &module,
                                           const QString &profile, const std::optional<JDK> &jdk,
                                         const QStringList &extraArgs = {});

/// 该模块有哪些可用的启动方式
QStringList availableRunTasks(const Module &module);

/// shell 引用辅助(测试用)
QString quoteIfNeeded(const QString &s);

} // namespace TaskPlanner
} // namespace jrunner
