#include "models.h"

#include <QJsonArray>
#include <QSet>

namespace jrunner {
namespace {

QString toolName(BuildTool tool)
{
    return tool == BuildTool::Gradle ? QStringLiteral("gradle") : QStringLiteral("maven");
}

std::optional<BuildTool> parseTool(const QJsonValue &value)
{
    if (value.toString() == QLatin1String("gradle"))
        return BuildTool::Gradle;
    if (value.toString() == QLatin1String("maven"))
        return BuildTool::Maven;
    return std::nullopt;
}

bool isInteger(const QJsonValue &value)
{
    return value.isDouble() && value.toDouble() == static_cast<double>(value.toInt());
}

bool hasFields(const QJsonObject &object, const QStringList &strings, const QStringList &booleans)
{
    for (const QString &key : strings) {
        if (!object.value(key).isString())
            return false;
    }
    for (const QString &key : booleans) {
        if (!object.value(key).isBool())
            return false;
    }
    return true;
}

} // namespace

QJsonObject Module::toJson() const
{
    return {
        {QStringLiteral("path"), path},
        {QStringLiteral("name"), name},
        {QStringLiteral("absolutePath"), absolutePath},
        {QStringLiteral("language"), language},
        {QStringLiteral("isSpringBoot"), isSpringBoot},
        {QStringLiteral("hasJib"), hasJib},
        {QStringLiteral("tool"), toolName(tool)},
        {QStringLiteral("javaVersion"), javaVersion ? QJsonValue(*javaVersion) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("availableProfiles"), QJsonArray::fromStringList(availableProfiles)},
        {QStringLiteral("defaultProfile"), defaultProfile},
        {QStringLiteral("hasDefaultProfile"), hasDefaultProfile},
    };
}

std::optional<Module> Module::fromJson(const QJsonObject &object)
{
    if (!hasFields(object,
                   {QStringLiteral("path"), QStringLiteral("name"), QStringLiteral("absolutePath"),
                    QStringLiteral("language"), QStringLiteral("defaultProfile")},
                   {QStringLiteral("isSpringBoot"), QStringLiteral("hasJib"), QStringLiteral("hasDefaultProfile")}))
        return std::nullopt;
    const auto tool = parseTool(object.value(QStringLiteral("tool")));
    const QJsonValue java = object.value(QStringLiteral("javaVersion"));
    const QJsonValue profiles = object.value(QStringLiteral("availableProfiles"));
    if (!tool || (!java.isNull() && !isInteger(java)) || !profiles.isArray())
        return std::nullopt;

    Module module;
    module.path = object.value(QStringLiteral("path")).toString();
    module.name = object.value(QStringLiteral("name")).toString();
    module.absolutePath = object.value(QStringLiteral("absolutePath")).toString();
    module.language = object.value(QStringLiteral("language")).toString();
    module.isSpringBoot = object.value(QStringLiteral("isSpringBoot")).toBool();
    module.hasJib = object.value(QStringLiteral("hasJib")).toBool();
    module.tool = *tool;
    if (!java.isNull())
        module.javaVersion = java.toInt();
    for (const QJsonValue &profile : profiles.toArray()) {
        if (!profile.isString())
            return std::nullopt;
        module.availableProfiles.append(profile.toString());
    }
    module.defaultProfile = object.value(QStringLiteral("defaultProfile")).toString();
    module.hasDefaultProfile = object.value(QStringLiteral("hasDefaultProfile")).toBool();
    return module;
}

QJsonObject Project::toJson() const
{
    QJsonArray moduleArray;
    for (const Module &module : modules)
        moduleArray.append(module.toJson());
    QString languageName;
    switch (language) {
    case ProjectLanguage::Kotlin: languageName = QStringLiteral("kotlin"); break;
    case ProjectLanguage::Java: languageName = QStringLiteral("java"); break;
    case ProjectLanguage::Mixed: languageName = QStringLiteral("mixed"); break;
    }
    return {
        {QStringLiteral("name"), name},
        {QStringLiteral("path"), path},
        {QStringLiteral("tool"), toolName(tool)},
        {QStringLiteral("language"), languageName},
        {QStringLiteral("modules"), moduleArray},
        {QStringLiteral("requiredJava"), requiredJava},
        {QStringLiteral("hasRequiredJava"), hasRequiredJava},
        {QStringLiteral("hasWrapper"), hasWrapper},
    };
}

std::optional<Project> Project::fromJson(const QJsonObject &object)
{
    if (!hasFields(object, {QStringLiteral("name"), QStringLiteral("path"), QStringLiteral("language")},
                   {QStringLiteral("hasRequiredJava"), QStringLiteral("hasWrapper")}))
        return std::nullopt;
    const auto tool = parseTool(object.value(QStringLiteral("tool")));
    const QJsonValue modules = object.value(QStringLiteral("modules"));
    if (!tool || !modules.isArray() || !isInteger(object.value(QStringLiteral("requiredJava"))))
        return std::nullopt;

    Project project;
    project.name = object.value(QStringLiteral("name")).toString();
    project.path = object.value(QStringLiteral("path")).toString();
    if (project.path.isEmpty())
        return std::nullopt;
    project.tool = *tool;
    const QString language = object.value(QStringLiteral("language")).toString();
    if (language == QLatin1String("kotlin"))
        project.language = ProjectLanguage::Kotlin;
    else if (language == QLatin1String("java"))
        project.language = ProjectLanguage::Java;
    else if (language == QLatin1String("mixed"))
        project.language = ProjectLanguage::Mixed;
    else
        return std::nullopt;
    QSet<QString> seen;
    for (const QJsonValue &value : modules.toArray()) {
        if (!value.isObject())
            return std::nullopt;
        const auto module = Module::fromJson(value.toObject());
        if (!module || seen.contains(module->path))
            return std::nullopt;
        seen.insert(module->path);
        project.modules.append(*module);
    }
    project.requiredJava = object.value(QStringLiteral("requiredJava")).toInt();
    project.hasRequiredJava = object.value(QStringLiteral("hasRequiredJava")).toBool();
    project.hasWrapper = object.value(QStringLiteral("hasWrapper")).toBool();
    return project;
}

} // namespace jrunner
