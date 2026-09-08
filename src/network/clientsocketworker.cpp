#include "clientsocketworker.h"

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

    // 阶段一只建立线程生命周期；阶段二在这里创建传输层和 BackendClient。
    m_initialized = true;
    emit initialized();
}

void ClientSocketWorker::shutdown()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_initialized) {
        emit stopped();
        return;
    }

    m_initialized = false;
    emit stopped();
}

void ClientSocketWorker::sendFrame(int messageType, const QJsonObject &payload)
{
    Q_ASSERT(QThread::currentThread() == thread());
    Q_UNUSED(payload)
    emit frameSendFailed(
        messageType,
        m_initialized
            ? QStringLiteral("Network transport is not installed yet.")
            : QStringLiteral("Network worker is not running."));
}

