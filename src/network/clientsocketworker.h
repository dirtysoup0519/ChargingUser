#pragma once

#include "common/connectionstate.h"

#include <QJsonObject>
#include <QObject>
#include <QString>

class BackendClient;
class QtNetworkTransport;

/**
 * 客户端网络线程中的私有 Worker。
 *
 * 构造函数不创建任何 socket 或定时器。所有具有线程亲和性的网络对象必须在
 * initialize() 中创建，确保它们属于 Worker 所在的 QThread。
 */
class ClientSocketWorker final : public QObject
{
    Q_OBJECT

public:
    ClientSocketWorker(const QString &host, quint16 port);

public slots:
    void initialize();
    void shutdown();
    void sendFrame(int messageType, const QJsonObject &payload);

signals:
    void initialized();
    void stopped();
    void frameReceived(int messageType, const QJsonObject &payload);
    void frameSendFailed(int messageType, const QString &message);
    void connectionStateChanged(ConnectionState state);
    void networkError(const QString &message);

private:
    QString m_host;
    quint16 m_port = 0;
    QtNetworkTransport *m_transport = nullptr;
    BackendClient *m_backend = nullptr;
    bool m_initialized = false;
};
