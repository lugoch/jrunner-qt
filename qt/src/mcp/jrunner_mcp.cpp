// 【jrunner-mcp】stdio MCP 桥:把外部 agent 的 MCP 请求转发到运行中 jrunner 的
// 本地控制 socket(jrunner-control)。仅做协议转换,不持有业务状态。
#include <QByteArray>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocalSocket>
#include <QSocketNotifier>

#include <cstdio>
#include <unistd.h>

namespace {

const char *kControlServer = "jrunner-control";
QByteArray g_buffer;

void sendJson(const QJsonObject &obj)
{
    const QByteArray line = QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
    ::write(1, line.constData(), static_cast<size_t>(line.size()));
}

QJsonObject makeResponse(const QJsonValue &id, const QJsonObject &result)
{
    QJsonObject resp;
    resp.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    resp.insert(QStringLiteral("id"), id);
    resp.insert(QStringLiteral("result"), result);
    return resp;
}

QJsonObject makeError(const QJsonValue &id, int code, const QString &message)
{
    QJsonObject err;
    err.insert(QStringLiteral("code"), code);
    err.insert(QStringLiteral("message"), message);
    QJsonObject resp;
    resp.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    resp.insert(QStringLiteral("id"), id);
    resp.insert(QStringLiteral("error"), err);
    return resp;
}

/// 连接 GUI 控制 socket 并发送一条控制请求,返回 {ok, result/error}
QJsonObject callControl(const QString &method, const QJsonObject &params)
{
    QLocalSocket socket;
    socket.connectToServer(QString::fromLatin1(kControlServer));
    if (!socket.waitForConnected(3000)) {
        QJsonObject out;
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("error"),
                   QStringLiteral("无法连接 jrunner:应用未运行或 MCP 控制未启用"));
        return out;
    }
    QJsonObject req;
    req.insert(QStringLiteral("id"), 1);
    req.insert(QStringLiteral("method"), method);
    req.insert(QStringLiteral("params"), params);
    socket.write(QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n');
    socket.flush();
    if (!socket.waitForReadyRead(8000)) {
        QJsonObject out;
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("error"), QStringLiteral("jrunner 控制响应超时"));
        return out;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(socket.readLine());
    if (!doc.isObject()) {
        QJsonObject out;
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("error"), QStringLiteral("jrunner 控制响应无效"));
        return out;
    }
    return doc.object();
}

QJsonObject toolDef(const QString &name, const QString &desc, const QJsonObject &schema)
{
    QJsonObject tool;
    tool.insert(QStringLiteral("name"), name);
    tool.insert(QStringLiteral("description"), desc);
    tool.insert(QStringLiteral("inputSchema"), schema);
    return tool;
}

QJsonObject props(const QStringList &names)
{
    QJsonObject properties;
    for (const QString &n : names) {
        QJsonObject p;
        p.insert(QStringLiteral("type"), QStringLiteral("string"));
        properties.insert(n, p);
    }
    QJsonObject schema;
    schema.insert(QStringLiteral("type"), QStringLiteral("object"));
    schema.insert(QStringLiteral("properties"), properties);
    schema.insert(QStringLiteral("required"), QJsonArray::fromStringList(names));
    return schema;
}

QJsonArray toolList()
{
    QJsonArray tools;
    tools.append(toolDef(QStringLiteral("list_projects"),
                         QStringLiteral("列出 jrunner 中所有项目及其模块与运行状态"),
                         props({})));
    tools.append(toolDef(QStringLiteral("list_modules"),
                         QStringLiteral("列出指定项目的模块与运行状态"),
                         props({ QStringLiteral("project") })));
    tools.append(toolDef(QStringLiteral("start_module"),
                         QStringLiteral("启动指定项目的模块"),
                         props({ QStringLiteral("project"), QStringLiteral("module") })));
    tools.append(toolDef(QStringLiteral("stop_module"),
                         QStringLiteral("停止指定项目的模块"),
                         props({ QStringLiteral("project"), QStringLiteral("module") })));
    tools.append(toolDef(QStringLiteral("build_module"),
                         QStringLiteral("打包指定项目的模块"),
                         props({ QStringLiteral("project"), QStringLiteral("module") })));
    tools.append(toolDef(QStringLiteral("module_status"),
                         QStringLiteral("查询模块运行状态、端口与最近日志"),
                         props({ QStringLiteral("project"), QStringLiteral("module") })));
    tools.append(toolDef(QStringLiteral("stop_all"),
                         QStringLiteral("停止所有正在运行的模块"), props({})));
    return tools;
}

QString controlMethodForTool(const QString &tool)
{
    if (tool == QLatin1String("module_status"))
        return QStringLiteral("status");
    return tool;
}

void handleLine(const QByteArray &line)
{
    const QJsonDocument doc = QJsonDocument::fromJson(line);
    if (!doc.isObject())
        return;
    const QJsonObject req = doc.object();
    const QString method = req.value(QStringLiteral("method")).toString();
    const QJsonValue id = req.value(QStringLiteral("id"));
    const QJsonObject params = req.value(QStringLiteral("params")).toObject();
    const bool isRequest = !id.isUndefined() && !id.isNull();

    if (method == QLatin1String("initialize")) {
        QJsonObject result;
        result.insert(QStringLiteral("protocolVersion"),
                      params.value(QStringLiteral("protocolVersion")).toString(QStringLiteral("2024-11-05")));
        QJsonObject caps;
        caps.insert(QStringLiteral("tools"), QJsonObject{});
        result.insert(QStringLiteral("capabilities"), caps);
        QJsonObject info;
        info.insert(QStringLiteral("name"), QStringLiteral("jrunner-mcp"));
        info.insert(QStringLiteral("version"), QStringLiteral("0.1.0"));
        result.insert(QStringLiteral("serverInfo"), info);
        sendJson(makeResponse(id, result));
    } else if (method == QLatin1String("ping")) {
        sendJson(makeResponse(id, QJsonObject{}));
    } else if (method == QLatin1String("tools/list")) {
        QJsonObject result;
        result.insert(QStringLiteral("tools"), toolList());
        sendJson(makeResponse(id, result));
    } else if (method == QLatin1String("tools/call")) {
        const QString tool = params.value(QStringLiteral("name")).toString();
        const QJsonObject args = params.value(QStringLiteral("arguments")).toObject();
        const QJsonObject ctrl = callControl(controlMethodForTool(tool), args);
        const bool ok = ctrl.value(QStringLiteral("ok")).toBool(false);
        QJsonObject content;
        content.insert(QStringLiteral("type"), QStringLiteral("text"));
        content.insert(QStringLiteral("text"),
                       ok ? QString::fromUtf8(QJsonDocument(ctrl.value(QStringLiteral("result")).toObject())
                                                  .toJson(QJsonDocument::Compact))
                          : ctrl.value(QStringLiteral("error")).toString());
        QJsonObject result;
        result.insert(QStringLiteral("content"), QJsonArray{ content });
        result.insert(QStringLiteral("isError"), !ok);
        sendJson(makeResponse(id, result));
    } else if (isRequest) {
        sendJson(makeError(id, -32601, QStringLiteral("method not found: %1").arg(method)));
    }
    // 通知(无 id)不回复
}

void onStdinReady()
{
    char buf[65536];
    const qint64 n = ::read(0, buf, sizeof(buf));
    if (n <= 0) {
        QCoreApplication::quit();
        return;
    }
    g_buffer.append(buf, static_cast<int>(n));
    int idx;
    while ((idx = g_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = g_buffer.left(idx);
        g_buffer.remove(0, idx + 1);
        if (!line.trimmed().isEmpty())
            handleLine(line);
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("jrunner-mcp"));
    QSocketNotifier notifier(0, QSocketNotifier::Read);
    QObject::connect(&notifier, &QSocketNotifier::activated, &app, []() { onStdinReady(); });
    return app.exec();
}