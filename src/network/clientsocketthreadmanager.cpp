#include "clientsocketthreadmanager.h"

#include "clientsocketworker.h"

#include <QMetaObject>
#include <QThread>

ClientSocketThreadManager::ClientSocketThreadManager(const QString &host,
                                                     quint16 port,
                                                     QObject *parent)
    : IBackendChannel(parent)
    , m_host(host)
    , m_port(port)
{
    qRegisterMetaType<ConnectionState>();
}

ClientSocketThreadManager::~ClientSocketThreadManager()
{
    stopAndWait();
}

ConnectionState ClientSocketThreadManager::connectionState() const
{
    return m_state;
}

bool ClientSocketThreadManager::isNetworkThreadRunning() const
{
    return m_thread && m_thread->isRunning();
}

void ClientSocketThreadManager::start()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (isNetworkThreadRunning()) {
        return;
    }

    auto *networkThread = new QThread(this);
    auto *worker = new ClientSocketWorker(m_host, m_port);
    worker->moveToThread(networkThread);

    m_thread = networkThread;
    m_worker = worker;
    m_stopRequested = false;

    connect(networkThread, &QThread::started,
            worker, &ClientSocketWorker::initialize);
    connect(this, &ClientSocketThreadManager::stopWorkerRequested,
            worker, &ClientSocketWorker::shutdown, Qt::QueuedConnection);
    connect(this, &ClientSocketThreadManager::sendFrameRequested,
            worker, &ClientSocketWorker::sendFrame, Qt::QueuedConnection);

    connect(worker, &ClientSocketWorker::initialized,
            this, &ClientSocketThreadManager::handleWorkerInitialized);
    connect(worker, &ClientSocketWorker::frameReceived,
            this, &IBackendChannel::frameReceived);
    connect(worker, &ClientSocketWorker::frameSendFailed,
            this, &IBackendChannel::frameSendFailed);
    connect(worker, &ClientSocketWorker::connectionStateChanged,
            this, &ClientSocketThreadManager::handleWorkerStateChanged);
    connect(worker, &ClientSocketWorker::networkError,
            this, &IBackendChannel::networkError);

    // quit() 是线程安全的；直接调用避免主线程析构等待时依赖主事件循环派发。
    connect(worker, &ClientSocketWorker::stopped,
            networkThread, &QThread::quit, Qt::DirectConnection);
    connect(networkThread, &QThread::finished,
            worker, &QObject::deleteLater);
    connect(networkThread, &QThread::finished, this,
            [this, networkThread] { handleThreadFinished(networkThread); });

    networkThread->start();
}

void ClientSocketThreadManager::stop()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!isNetworkThreadRunning() || m_stopRequested) {
        return;
    }
    m_stopRequested = true;
    emit stopWorkerRequested();
}

void ClientSocketThreadManager::sendFrame(int messageType,
                                          const QJsonObject &payload)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!isNetworkThreadRunning() || m_stopRequested) {
        emit frameSendFailed(messageType,
                             QStringLiteral("Network worker is not running."));
        return;
    }
    emit sendFrameRequested(messageType, payload);
}

void ClientSocketThreadManager::handleWorkerInitialized()
{
    emit networkThreadStarted();
}

void ClientSocketThreadManager::handleWorkerStateChanged(ConnectionState state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit connectionStateChanged(state);
}

void ClientSocketThreadManager::handleThreadFinished(QThread *finishedThread)
{
    if (m_thread != finishedThread) {
        return;
    }

    m_worker.clear();
    m_thread.clear();
    m_stopRequested = false;
    handleWorkerStateChanged(ConnectionState::Disconnected);
    emit networkThreadStopped();
    finishedThread->deleteLater();
}

void ClientSocketThreadManager::stopAndWait()
{
    if (!m_thread) {
        return;
    }

    QThread *networkThread = m_thread.data();
    ClientSocketWorker *worker = m_worker.data();
    if (networkThread->isRunning() && worker) {
        QMetaObject::invokeMethod(worker, "shutdown", Qt::BlockingQueuedConnection);
        networkThread->quit();
        networkThread->wait();
    }
}

