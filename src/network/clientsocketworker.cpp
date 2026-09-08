#include "clientsocketworker.h"

#include "backendclient.h"
#include "qtnetworktransport.h"

#include <QThread>

ClientSocketWorker::ClientSocketWorker(const QString &host, quint16 port)
    : m_host(host)
    , m_port(port)
{
}

void ClientSocketWorker::initialize()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_initialized) {
        return;
    }

    // 必须在本槽中创建：构造 Worker 时调用方仍位于主线程。
    m_transport = new QtNetworkTransport(m_host, m_port, this);
    m_backend = new BackendClient(m_transport, this);

    connect(m_backend, &BackendClient::frameReceived,
            this, &ClientSocketWorker::frameReceived);
    connect(m_backend, &BackendClient::connectionStateChanged,
            this, &ClientSocketWorker::connectionStateChanged);
    connect(m_backend, &BackendClient::networkError,
            this, &ClientSocketWorker::networkError);

    m_initialized = true;
    emit initialized();
    m_backend->start();
}

void ClientSocketWorker::shutdown()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_initialized) {
        emit stopped();
        return;
    }

    if (m_backend) {
        m_backend->shutdown();
    }
    m_initialized = false;
    emit stopped();
}

void ClientSocketWorker::sendFrame(int messageType, const QJsonObject &payload)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_initialized || !m_backend) {
        emit frameSendFailed(messageType,
                             QStringLiteral("Network worker is not running."));
        return;
    }
    if (!m_backend->sendFrame(messageType, payload)) {
        emit frameSendFailed(
            messageType,
            m_backend->connectionState() == ConnectionState::Connected
                ? QStringLiteral("Protocol frame could not be queued.")
                : QStringLiteral("Backend is not connected."));
    }
}
