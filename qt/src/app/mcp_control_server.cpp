#include "mcp_control_server.h"

#include "app_store.h"
#include "core/debug_log.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>

namespace jrunner {

namespace {

QString stateName(const TaskState &state)
{
    switch (state.kind) {
    case TaskState::Starting: return QStringLiteral("starting");
    case TaskState::Running: return QStringLiteral("running");
    case TaskState::Stopping: return QStringLiteral("stopping");
    case TaskState::Success: return QStringLiteral("success");
    case TaskState::Failed: return QStringLiteral("failed");
    default: return QStringLiteral("idle");
    }
}

QJsonObject moduleJson(AppStore *store, const Project &project, const Module &module)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("path"), module.path);
    obj.insert(QStringLiteral("name"), module.name);
    ProcessTask *task = store->task(project.path, module.path);
    obj.insert(QStringLiteral("running"), task && task->state().isActive());
    obj.insert(QStringLiteral("state"), task ? stateName(task->state()) : QStringLiteral("idle"));
    obj.insert(QStringLiteral("port"), task ? task->detectedPort() : 0);
    return obj;
}

const Module *findModule(const Project &project, const QString &path)
{
    for (const Module &m : project.modules)
        if (m.path == path)
            return &m;
    return nullptr;
}

} // namespace

QString McpControlServer::serverName()
{
    return QStringLiteral("jrunner-control");
}

McpControlServer::McpControlServer(AppStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
    connect(&m_server, &QLocalServer::newConnection, this, &McpControlServer::handleConnection);
}

bool McpControlServer::start()
{
    if (m_server.isListening())
        return true;
    QLocalServer::removeServer(serverName());
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server.listen(serverName())) {
        debugLog(QStringLiteral("mcp"), QStringLiteral("控制服务监听失败: %1").arg(m_server.errorString()));
        return false;
    }
    return true;
}

void McpControlServer::stop()
{
    m_server.close();
}

void McpControlServer::handleConnection()
{
    while (QLocalSocket *socket = m_server.nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
            while (socket->canReadLine())
                handleLine(socket, socket->readLine());
        });
    }
}

void McpControlServer::handleLine(QLocalSocket *socket, const QByteArray &line)
{
    const QJsonDocument doc = QJsonDocument::fromJson(line);
    if (!doc.isObject())
        return;
    const QJsonObject req = doc.object();
    const QJsonValue id = req.value(QStringLiteral("id"));
    const QString method = req.value(QStringLiteral("method")).toString();
    const QJsonObject params = req.value(QStringLiteral("params")).toObject();

    QString error;
    QJsonObject result;

    if (method == QLatin1String("ping")) {
        result.insert(QStringLiteral("app"), QStringLiteral("jrunner"));
        result.insert(QStringLiteral("mcp"), true);
    } else if (method == QLatin1String("list_projects")) {
        QJsonArray arr;
        for (const Project &p : m_store->projects()) {
            QJsonObject po;
            po.insert(QStringLiteral("path"), p.path);
            po.insert(QStringLiteral("name"), p.name);
            po.insert(QStringLiteral("tool"), buildToolDisplayName(p.tool));
            QJsonArray mods;
            for (const Module &m : p.modules)
                mods.append(moduleJson(m_store, p, m));
            po.insert(QStringLiteral("modules"), mods);
            arr.append(po);
        }
        result.insert(QStringLiteral("projects"), arr);
    } else if (method == QLatin1String("list_modules")) {
        const auto project = m_store->projectAt(params.value(QStringLiteral("project")).toString());
        if (!project) {
            error = QStringLiteral("project not found");
        } else {
            QJsonArray mods;
            for (const Module &m : project->modules)
                mods.append(moduleJson(m_store, *project, m));
            result.insert(QStringLiteral("project"), project->path);
            result.insert(QStringLiteral("modules"), mods);
        }
    } else if (method == QLatin1String("start_module")) {
        const QString pp = params.value(QStringLiteral("project")).toString();
        const QString mp = params.value(QStringLiteral("module")).toString();
        if (!m_store->runModuleAt(pp, mp))
            error = QStringLiteral("project/module not found");
        else
            result.insert(QStringLiteral("running"), m_store->isRunning(pp, mp));
    } else if (method == QLatin1String("stop_module")) {
        const QString pp = params.value(QStringLiteral("project")).toString();
        const QString mp = params.value(QStringLiteral("module")).toString();
        if (!m_store->stopModuleAt(pp, mp))
            error = QStringLiteral("project/module not found");
        else
            result.insert(QStringLiteral("stopped"), true);
    } else if (method == QLatin1String("build_module")) {
        const QString pp = params.value(QStringLiteral("project")).toString();
        const QString mp = params.value(QStringLiteral("module")).toString();
        if (!m_store->buildModuleAt(pp, mp))
            error = QStringLiteral("project/module not found");
        else
            result.insert(QStringLiteral("buildStarted"), true);
    } else if (method == QLatin1String("status")) {
        const QString pp = params.value(QStringLiteral("project")).toString();
        const QString mp = params.value(QStringLiteral("module")).toString();
        ProcessTask *task = m_store->task(pp, mp);
        result.insert(QStringLiteral("running"), task && task->state().isActive());
        result.insert(QStringLiteral("state"), task ? stateName(task->state()) : QStringLiteral("idle"));
        result.insert(QStringLiteral("port"), task ? task->detectedPort() : 0);
        QJsonArray tail;
        if (task) {
            const QList<LogLine> &lines = task->lines();
            const int from = qMax(0, lines.size() - 40);
            for (int i = from; i < lines.size(); ++i)
                tail.append(lines.at(i).text);
        }
        result.insert(QStringLiteral("logTail"), tail);
    } else if (method == QLatin1String("stop_all")) {
        m_store->stopAll();
        result.insert(QStringLiteral("stopped"), true);
    } else {
        error = QStringLiteral("unknown method: %1").arg(method);
    }

    QJsonObject resp;
    resp.insert(QStringLiteral("id"), id);
    resp.insert(QStringLiteral("ok"), error.isEmpty());
    if (error.isEmpty())
        resp.insert(QStringLiteral("result"), result);
    else
        resp.insert(QStringLiteral("error"), error);
    socket->write(QJsonDocument(resp).toJson(QJsonDocument::Compact) + '\n');
    socket->flush();
}

} // namespace jrunner