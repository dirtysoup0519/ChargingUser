#pragma once

#include <QObject>
#include <QJsonObject>

#include "common/connectionstate.h"
#include "inetworktransport.h"

class MassageHandler;
class QTimer;

/* 连接管理层（合同 §7 步骤 7）：
 *  - 持有传输层引用（非拥有，生命周期由装配层保证）与共享编解码器 MassageHandler，
 *    把字节流拆成业务帧；
 *  - 心跳（30s）与自动重连（5s），重连前调用 MassageHandler::reset() 丢弃残留半包；
 *  - 上层只看到 frameReceived(type, json) 与 connectionStateChanged(state)。
 * 本类不解析业务语义，不维护请求关联——那是各模块适配器的职责。
 */
class BackendClient final : public QObject
{
    Q_OBJECT

public:
    explicit BackendClient(INetworkTransport *transport, QObject *parent = nullptr);
    ~BackendClient() override;

    void start();
    void shutdown();
    bool switchEndpoint(const QString &host, quint16 port);

    // 供测试与部署调节；默认 RECONNECT_INTERVAL_MS
    void setReconnectIntervalMs(int intervalMs);
    // 供测试与部署调节；默认 HEARTBEAT_INTERVAL_MS
    void setHeartbeatIntervalMs(int intervalMs);

    ConnectionState connectionState() const;

    // 未连接或发送失败返回 false
    bool sendFrame(int msgType, const QJsonObject &payload = QJsonObject());

signals:
    void frameReceived(int msgType, const QJsonObject &payload);
    void connectionStateChanged(ConnectionState state);
    // 诊断信息供应用层展示或记录；不得依赖服务端自由文本判断业务分支。
    void networkError(const QString &message);

private:
    void setState(ConnectionState state);
    void handleConnected();
    void handleDisconnected();
    void handleFrame(int msgType, const QByteArray &payload);
    void attemptReconnect();

    INetworkTransport *m_transport;
    MassageHandler *m_handler;
    ConnectionState m_state = ConnectionState::Disconnected;
    QTimer *m_heartbeatTimer;
    QTimer *m_reconnectTimer;
    bool m_started = false;
};
