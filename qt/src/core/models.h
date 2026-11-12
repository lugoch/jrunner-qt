#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace jrunner {

// 【数据模型】对应 jrunner macOS 版 Models.swift 的领域模型。

/// 构建工具类型
enum class BuildTool {
    Gradle,
    Maven,
};

inline QString buildToolDisplayName(BuildTool tool)
{
    return tool == BuildTool::Gradle ? QStringLiteral("Gradle") : QStringLiteral("Maven");
}

/// 可运行模块(一个 Gradle 子项目 / Maven module)
struct Module {
    /// 相对项目根的路径,例如 ":community-portal"
    QString path;
    /// 目录名,例如 "community-portal"
    QString name;
    /// 绝对路径
    QString absolutePath;
    /// 语言: "Kotlin" / "Java"
    QString language;
    /// 是否包含 Spring Boot 启动类
    bool isSpringBoot = false;
    /// 是否检测到 jib 插件
    bool hasJib = false;
    /// 构建工具
    BuildTool tool = BuildTool::Gradle;
    /// 该模块声明的 JDK 需求(如 toolchain 17)
    std::optional<int> javaVersion;
    /// 可用的 Spring Profile(来自 application-*.yaml)
    QStringList availableProfiles;
    /// 配置里声明的默认 profile
    QString defaultProfile;
    bool hasDefaultProfile = false;

    QJsonObject toJson() const;
    static std::optional<Module> fromJson(const QJsonObject &object);

    /// 展示用路径:去掉 Gradle 的前导冒号,根模块回退为项目名
    QString displayPath() const
    {
        const QString trimmed = path.startsWith(QLatin1Char(':'))
            ? path.mid(1)
            : path;
        return trimmed.isEmpty() ? name : trimmed;
    }
};

using ModuleList = QList<Module>;

enum class ProjectLanguage {
    Kotlin,
    Java,
    Mixed,
};

inline QString projectLanguageDisplayName(ProjectLanguage lang)
{
    switch (lang) {
    case ProjectLanguage::Kotlin: return QStringLiteral("Kotlin");
    case ProjectLanguage::Java: return QStringLiteral("Java");
    case ProjectLanguage::Mixed: return QStringLiteral("Java/Kotlin");
    }
    return QStringLiteral("Java");
}

/// 一个项目根目录
struct Project {
    QString name;
    QString path;
    BuildTool tool = BuildTool::Gradle;
    ProjectLanguage language = ProjectLanguage::Java;
    ModuleList modules;
    /// 扫描到的 JDK 需求集合(取众数)
    int requiredJava = 0;      // 0 = 未检测到
    bool hasRequiredJava = false;
    /// 是否有 gradle wrapper
    bool hasWrapper = false;

    QJsonObject toJson() const;
    static std::optional<Project> fromJson(const QJsonObject &object);
};

} // namespace jrunner
