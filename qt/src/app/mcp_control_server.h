#pragma once

#include <QLocalServer>
#include <QObject>

class QLocalSocket;

namespace jrunner {

class AppStore;

// 【MCP 控制服务】在本地 Unix socket 上暴露 JSON-lines 控制接口,
// 供 jrunner-mcp(stdio MCP 桥)转发外部 agent 的启停/查询请求。
// 由 AppConfig::mcpEnabled 开关控制;仅同一用户可连接。

class McpControlServer : public QObject {
    Q_OBJECT
public:
    explicit McpControlServer(AppStore *store, QObject *parent = nullptr);

    bool start();
    void stop();
    bool isListening() const { return m_server.isListening(); }
    static QString serverName();

private:
    void handleConnection();
    void handleLine(QLocalSocket *socket, const QByteArray &line);

    AppStore *m_store;
    QLocalServer m_server;
};

} // namespace jrunner