#pragma once

#include "inetworktransport.h"

class QTcpSocket;

/* QTcpSocket 对 INetworkTransport 的默认实现。
 * 地址来自构造参数；为保持简单，不内置连接参数持久化（装配层负责提供）。
 */
class QtNetworkTransport final : public INetworkTransport
{
    Q_OBJECT

public:
    QtNetworkTransport(const QString &host, quint16 port, QObject *parent = nullptr);
    ~QtNetworkTransport() override;

    void connectToServer() override;
    void disconnectFromServer() override;
    bool send(const QByteArray &data) override;
    bool isConnected() const override;

private:
    QString m_host;
    quint16 m_port;
    QTcpSocket *m_socket;
};
