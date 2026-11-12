#pragma once

#include "models.h"

#include <optional>

namespace jrunner {

// 【项目扫描】识别 Gradle / Maven 项目及其模块,对应 macOS 版 ProjectScanner。
// 全部基于文件系统解析,不调用外部进程。
namespace ProjectScanner {

/// 扫描一个目录,返回项目信息。目录不是项目则返回 nullopt。
std::optional<Project> scan(const QString &path);

/// 从 settings.gradle 内容中提取 include(...) 的模块路径(测试用)
QStringList parseGradleIncludes(const QString &content);

/// 从 pom.xml 内容中提取 <modules> 块(测试用)
QStringList parseMavenModules(const QString &content);

/// 从模块目录检测默认 profile(测试用)
QString detectDefaultProfileInDir(const QString &dir);

/// 检测模块 JDK 需求(测试用)
std::optional<int> detectModuleJavaVersion(const QString &dir);

} // namespace ProjectScanner

} // namespace jrunner
