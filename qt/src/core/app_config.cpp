#include "app_config.h"

#include "debug_log.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace jrunner {

namespace {

ModuleConfig moduleConfigFromJson(const QJsonObject &obj)
{
    ModuleConfig cfg;
    cfg.profile = obj.value(QStringLiteral("profile")).toString();
    if (obj.contains(QStringLiteral("javaVersion")))
        cfg.javaVersion = obj.value(QStringLiteral("javaVersion")).toInt();
    const QJsonObject env = obj.value(QStringLiteral("envVars")).toObject();
    for (auto it = env.begin(); it != env.end(); ++it)
        cfg.envVars.insert(it.key(), it.value().toString());
    const QJsonArray args = obj.value(QStringLiteral("extraArgs")).toArray();
    for (const auto &v : args)
        cfg.extraArgs.append(v.toString());
    return cfg;
}

QJsonObject moduleConfigToJson(const ModuleConfig &cfg)
{
    QJsonObject obj;
    if (!cfg.profile.isEmpty())
        obj.insert(QStringLiteral("profile"), cfg.profile);
    if (cfg.javaVersion > 0)
        obj.insert(QStringLiteral("javaVersion"), cfg.javaVersion);
    if (!cfg.envVars.isEmpty()) {
        QJsonObject env;
        for (auto it = cfg.envVars.constBegin(); it != cfg.envVars.constEnd(); ++it)
            env.insert(it.key(), it.value());
        obj.insert(QStringLiteral("envVars"), env);
    }
    if (!cfg.extraArgs.isEmpty()) {
        QJsonArray args;
        for (const QString &a : cfg.extraArgs)
            args.append(a);
        obj.insert(QStringLiteral("extraArgs"), args);
    }
    return obj;
}

} // namespace

AppConfig AppConfig::load()
{
    AppConfig cfg;
    QFile file(configPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return cfg;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        debugLog(QStringLiteral("config"),
                 QStringLiteral("配置解析失败,使用默认值: %1").arg(parseError.errorString()));
        return cfg;
    }
    return AppConfig::fromJson(doc.object());
}

void AppConfig::save() const
{
    QDir dir(configDir());
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        debugLog(QStringLiteral("config"), QStringLiteral("无法创建配置目录: %1").arg(configDir()));
        return;
    }
    QFile file(configPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        debugLog(QStringLiteral("config"), QStringLiteral("无法写入配置: %1").arg(configPath()));
        return;
    }
    file.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
}

QString AppConfig::configDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
        + QStringLiteral("/jrunner");
}

QString AppConfig::configPath()
{
    return configDir() + QStringLiteral("/config.json");
}

QString AppConfig::logDir()
{
    return configDir() + QStringLiteral("/logs");
}

ModuleConfig AppConfig::config(const QString &project, const QString &module) const
{
    return projects.value(project).value(module);
}

void AppConfig::setConfig(const ModuleConfig &cfg, const QString &project, const QString &module)
{
    projects[project][module] = cfg;
}

QList<ModuleGroup> AppConfig::projectGroups(const QString &project) const
{
    return groups.value(project);
}

void AppConfig::setGroups(const QString &project, const QList<ModuleGroup> &list)
{
    if (list.isEmpty())
        groups.remove(project);
    else
        groups[project] = list;
}

QJsonObject AppConfig::toJson() const
{
    QJsonObject root;
    if (!projects.isEmpty()) {
        QJsonObject projectsObj;
        for (auto pit = projects.constBegin(); pit != projects.constEnd(); ++pit) {
            QJsonObject modulesObj;
            const QHash<QString, ModuleConfig> &modules = pit.value();
            for (auto mit = modules.constBegin(); mit != modules.constEnd(); ++mit)
                modulesObj.insert(mit.key(), moduleConfigToJson(mit.value()));
            projectsObj.insert(pit.key(), modulesObj);
        }
        root.insert(QStringLiteral("projects"), projectsObj);
    }
    if (!lastOpenedProject.isEmpty())
        root.insert(QStringLiteral("lastOpenedProject"), lastOpenedProject);
    root.insert(QStringLiteral("logLevelFilter"), logLevelFilter);
    root.insert(QStringLiteral("maxLogLines"), maxLogLines);
    root.insert(QStringLiteral("mcpEnabled"), mcpEnabled);
    if (!projectNames.isEmpty()) {
        QJsonObject names;
        for (auto it = projectNames.constBegin(); it != projectNames.constEnd(); ++it)
            names.insert(it.key(), it.value());
        root.insert(QStringLiteral("projectNames"), names);
    }
    if (!groups.isEmpty()) {
        QJsonObject groupsObj;
        for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
            QJsonArray arr;
            for (const ModuleGroup &g : it.value()) {
                QJsonObject go;
                go.insert(QStringLiteral("name"), g.name);
                go.insert(QStringLiteral("modules"), QJsonArray::fromStringList(g.modulePaths));
                arr.append(go);
            }
            groupsObj.insert(it.key(), arr);
        }
        root.insert(QStringLiteral("groups"), groupsObj);
    }
    return root;
}

AppConfig AppConfig::fromJson(const QJsonObject &root)
{
    AppConfig cfg;
    const QJsonObject projectsObj = root.value(QStringLiteral("projects")).toObject();
    for (auto pit = projectsObj.begin(); pit != projectsObj.end(); ++pit) {
        const QJsonObject modulesObj = pit.value().toObject();
        for (auto mit = modulesObj.begin(); mit != modulesObj.end(); ++mit)
            cfg.projects[pit.key()].insert(mit.key(), moduleConfigFromJson(mit.value().toObject()));
    }
    cfg.lastOpenedProject = root.value(QStringLiteral("lastOpenedProject")).toString();
    cfg.logLevelFilter = root.value(QStringLiteral("logLevelFilter")).toString(QStringLiteral("info"));
    cfg.maxLogLines = root.value(QStringLiteral("maxLogLines")).toInt(20000);
    cfg.mcpEnabled = root.value(QStringLiteral("mcpEnabled")).toBool(false);
    const QJsonObject namesObj = root.value(QStringLiteral("projectNames")).toObject();
    for (auto it = namesObj.begin(); it != namesObj.end(); ++it) {
        const QString name = it.value().toString();
        if (!name.isEmpty())
            cfg.projectNames.insert(it.key(), name);
    }
    const QJsonObject groupsObj = root.value(QStringLiteral("groups")).toObject();
    for (auto it = groupsObj.begin(); it != groupsObj.end(); ++it) {
        QList<ModuleGroup> list;
        for (const QJsonValue &v : it.value().toArray()) {
            const QJsonObject go = v.toObject();
            ModuleGroup g;
            g.name = go.value(QStringLiteral("name")).toString();
            for (const QJsonValue &m : go.value(QStringLiteral("modules")).toArray())
                g.modulePaths.append(m.toString());
            if (!g.name.isEmpty())
                list.append(g);
        }
        if (!list.isEmpty())
            cfg.groups[it.key()] = list;
    }
    return cfg;
}

} // namespace jrunner
