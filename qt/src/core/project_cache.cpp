#include "project_cache.h"

#include "app_config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

namespace jrunner::ProjectCache {

QString path()
{
    return AppConfig::configDir() + QStringLiteral("/projects.json");
}

std::optional<ProjectSnapshot> load(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;
    const QJsonObject root = document.object();
    const QJsonValue version = root.value(QStringLiteral("version"));
    const QJsonValue projects = root.value(QStringLiteral("projects"));
    if (!version.isDouble() || version.toDouble() != kVersion || !projects.isArray())
        return std::nullopt;
    for (const QString &key : {QStringLiteral("selectedProjectPath"), QStringLiteral("selectedModulePath")}) {
        if (root.contains(key) && !root.value(key).isString())
            return std::nullopt;
    }

    ProjectSnapshot snapshot;
    snapshot.selectedProjectPath = root.value(QStringLiteral("selectedProjectPath")).toString();
    snapshot.selectedModulePath = root.value(QStringLiteral("selectedModulePath")).toString();
    QSet<QString> seen;
    for (const QJsonValue &value : projects.toArray()) {
        if (!value.isObject())
            return std::nullopt;
        const auto project = Project::fromJson(value.toObject());
        if (!project || seen.contains(project->path))
            return std::nullopt;
        seen.insert(project->path);
        snapshot.projects.append(*project);
    }
    return snapshot;
}

bool save(const ProjectSnapshot &snapshot, const QString &filePath)
{
    QJsonArray projects;
    for (const Project &project : snapshot.projects)
        projects.append(project.toJson());
    const QJsonObject root{
        {QStringLiteral("version"), kVersion},
        {QStringLiteral("projects"), projects},
        {QStringLiteral("selectedProjectPath"), snapshot.selectedProjectPath},
        {QStringLiteral("selectedModulePath"), snapshot.selectedModulePath},
    };
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (!QDir().mkpath(QFileInfo(filePath).absolutePath()))
        return false;
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(data) != data.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

} // namespace jrunner::ProjectCache
