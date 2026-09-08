#include "backendclient.h"

#include "massagehandler.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QThread>
#include <QTimer>

BackendClient::BackendClient(INetworkTransport *transport, QObject *parent)
    : QObject(parent)
    , m_transport(transport)
    , m_handler(new MassageHandler(this))
    , m_heartbeatTimer(new QTimer(this))
    , m_reconnectTimer(new QTimer(this))
{
    Q_ASSERT(m_transport);
    // 非拥有指针：transport 生命周期由装配层保证长于本对象

    m_heartbeatTimer->setInterval(HEARTBEAT_INTERVAL_MS);
    m_reconnectTimer->setSingleShot(true);
    m_reconnectTimer->setInterval(RECONNECT_INTERVAL_MS);

    connect(m_transport, &INetworkTransport::connected,
            this, &BackendClient::handleConnected);
    connect(m_transport, &INetworkTransport::disconnected,
            this, &BackendClient::handleDisconnected);
    connect(m_transport, &INetworkTransport::dataReceived,
            m_handler, &MassageHandler::feed);
    connect(m_transport, &INetworkTransport::transportError, this,
            [this](const QString &message) {
        if (!m_started) {
            return; // shutdown 后到达的异步错误不能重新驱动连接状态机
        }
        emit networkError(message);
        // 首次连接被拒等场景没有 disconnected 信号，只有错误：
        // 必须在这里调度重连，否则会永远卡在 Connecting（已连接后的错误由
        // disconnected 信号处理，此处不重复触发）
        if (m_state == ConnectionState::Connecting) {
            setState(ConnectionState::Reconnecting);
            m_reconnectTimer->start();
        }
    });

    connect(m_handler, &MassageHandler::frameReady,
            this, &BackendClient::handleFrame);

    connect(m_heartbeatTimer, &QTimer::timeout, this, [this] {
        sendFrame(HEARTBEAT);
    });
    connect(m_reconnectTimer, &QTimer::timeout,
            this, &BackendClient::attemptReconnect);
}

BackendClient::~BackendClient() = default;

void BackendClient::start()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_started) {
        return;   // 重复 start 会把已连接状态错误地改为 Connecting
    }
    m_started = true;
    m_handler->reset();
    if (m_transport->isConnected()) {
        handleConnected();
        return;
    }
    setState(ConnectionState::Connecting);
    m_transport->connectToServer();
}

void BackendClient::setReconnectIntervalMs(int intervalMs)
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_reconnectTimer->setInterval(intervalMs);
}

void BackendClient::setHeartbeatIntervalMs(int intervalMs)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (intervalMs > 0) {
        m_heartbeatTimer->setInterval(intervalMs);
    }
}

void BackendClient::shutdown()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_started && m_state == ConnectionState::Disconnected
        && !m_transport->isConnected()) {
        return;
    }
    m_started = false;
    m_heartbeatTimer->stop();
    m_reconnectTimer->stop();
    m_handler->reset();
    m_transport->disconnectFromServer();
    setState(ConnectionState::Disconnected);
}

ConnectionState BackendClient::connectionState() const
{
    Q_ASSERT(QThread::currentThread() == thread());
    return m_state;
}

bool BackendClient::sendFrame(int msgType, const QJsonObject &payload)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state != ConnectionState::Connected) {
        return false;
    }

    // 协议规定 107 心跳是零长度载荷；QJsonObject() 经 pack() 会变成 "{}"，
    // 因此必须使用专用构造函数，不能把空 JSON 对象当作空载荷。
    const QByteArray frame = (msgType == HEARTBEAT && payload.isEmpty())
                           ? MassageHandler::makeHeartbeat()
                           : MassageHandler::pack(msgType, payload);
    if (frame.isEmpty()) {
        emit networkError(QStringLiteral("Protocol payload exceeds the permitted size."));
        return false;
    }
    return m_transport->send(frame);
}

void BackendClient::setState(ConnectionState state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit connectionStateChanged(m_state);
}

void BackendClient::handleConnected()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_started) {
        // connectToHost 的迟到成功事件可能发生在 shutdown 之后。
        m_transport->disconnectFromServer();
        return;
    }
    // 旧连接的残留半包不得进入新连接
    m_handler->reset();
    setState(ConnectionState::Connected);
    // 连接建立立即发一次心跳：部分服务端实现会把"连上后长时间无数据"的
    // 连接当作死连接关闭（远程联调实测 10.194.99.223 数秒即踢）。提前
    // 发送 107 证明客户端存活，随后仍按 30s 周期保活。
    m_transport->send(MassageHandler::makeHeartbeat());
    m_heartbeatTimer->start();
}

void BackendClient::handleDisconnected()
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_heartbeatTimer->stop();
    m_handler->reset();

    if (!m_started) {
        setState(ConnectionState::Disconnected);
        return;
    }

    setState(ConnectionState::Reconnecting);
    m_reconnectTimer->start();
}

void BackendClient::handleFrame(int msgType, const QByteArray &payload)
{
    Q_ASSERT(QThread::currentThread() == thread());
    // 只在已连接状态下交付业务帧，重连过渡期丢弃迟到数据
    if (m_state != ConnectionState::Connected) {
        return;
    }
    if (payload.isEmpty()) {
        emit frameReceived(msgType, QJsonObject());
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        // 不记录原始载荷，避免将手机号、头像或其他业务数据写入日志。
        emit networkError(
            QStringLiteral("Received invalid JSON object for message type %1.")
                .arg(msgType));
        return;
    }
    emit frameReceived(msgType, document.object());
}

void BackendClient::attemptReconnect()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_started || m_state == ConnectionState::Connected) {
        return;
    }
    setState(ConnectionState::Connecting);
    m_transport->connectToServer();
}
