#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <optional>

namespace jrunner {

// 【Shell 环境】对应 macOS 版 Shell.swift。
// GUI 应用不继承用户在 shell 配置文件里 export 的变量(数据库地址、Nacos 地址等),
// 这里通过 login shell 取回来,让行为与终端里执行一致。

struct ShellResult {
    int exitCode = -1;
    QString standardOutput;
    QString standardError;
    QString combined() const { return standardOutput + standardError; }
};

namespace Shell {

/// 一次性同步执行命令(调用方需在工作线程调用,避免阻塞 UI)
std::optional<ShellResult> run(const QString &launchPath, const QStringList &args,
                               const QString &cwd = QString(),
                               const QHash<QString, QString> &env = {});

/// 查找用户 PATH 中的可执行文件
QString which(const QString &name);

/// 读取 login shell 的完整环境变量(带缓存)
QHash<QString, QString> userEnvironment();

/// 获取用户真实 PATH,含常见安装位置兜底(带缓存)
QString userPath();

/// 清空缓存(测试用)
void invalidateCache();

} // namespace Shell
} // namespace jrunner
