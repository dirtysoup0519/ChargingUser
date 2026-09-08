#pragma once

#include "ibackendchannel.h"

#include <QPointer>
#include <QString>

class ClientSocketWorker;
class QThread;

/**
 * 主线程中的客户端网络门面。
 *
 * 本类拥有网络线程的生命周期，并通过 queued signal/slot 与 Worker 通信。
 * 它不直接创建或调用 QTcpSocket，因此业务层可以安全地在主线程使用此对象。
 */
class ClientSocketThreadManager final : public IBackendChannel
{
    Q_OBJECT

public:
    ClientSocketThreadManager(const QString &host, quint16 port,
                              QObject *parent = nullptr);
    ~ClientSocketThreadManager() override;

    ConnectionState connectionState() const override;
    bool isNetworkThreadRunning() const;

public slots:
    void start() override;
    void stop() override;
    void sendFrame(int messageType,
                   const QJsonObject &payload = QJsonObject()) override;

signals:
    void startWorkerRequested();
    void stopWorkerRequested();
    void sendFrameRequested(int messageType, const QJsonObject &payload);
    void networkThreadStarted();
    void networkThreadStopped();

private:
    void handleWorkerInitialized();
    void handleWorkerStateChanged(ConnectionState state);
    void handleThreadFinished(QThread *finishedThread);
    void stopAndWait();

    QString m_host;
    quint16 m_port = 0;
    ConnectionState m_state = ConnectionState::Disconnected;
    QPointer<QThread> m_thread;
    QPointer<ClientSocketWorker> m_worker;
    bool m_stopRequested = false;
};

