#pragma once

#include <QList>
#include <QString>

#include <optional>

namespace jrunner {

// 【JDK 解析】对应 macOS 版 JDKResolver.swift 的 Linux 适配版。
// macOS 用 /usr/libexec/java_home;Linux 无此工具,改为多源扫描:
// $JAVA_HOME、/usr/lib/jvm/*、~/.jdks(IntelliJ)、~/.sdkman、~/.asdf、PATH 上的 java 反解。

struct JDK {
    int version = 0;          // 主版本号,如 17
    QString fullVersion;      // 如 "17.0.15"
    QString vendor;           // 如 "Eclipse Temurin"
    QString home;             // JAVA_HOME 路径
    bool isCurrent = false;   // 是否为系统默认
};

using JDKList = QList<JDK>;

namespace JDKResolver {

/// 已安装的 JDK 列表(带缓存;forceRefresh 强制重新扫描)
JDKList installedJDKs(bool forceRefresh = false);

void invalidateCache();

/// 为指定版本选择最合适的 JDK:精确匹配 → >= 需求中最低 → 当前/最新
std::optional<JDK> resolve(std::optional<int> version);

/// 从 JDK 根目录读取 release 文件解析版本(测试用)
std::optional<QPair<int, QString>> parseReleaseFile(const QString &jdkHome);

} // namespace JDKResolver
} // namespace jrunner
