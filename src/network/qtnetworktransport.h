#pragma once

#include "inetworktransport.h"

#include <QByteArray>

class QTcpSocket;

/* QTcpSocket 对 INetworkTransport 的默认实现。
 * 地址来自构造参数，构造对象不会自动连接；连接生命周期由 BackendClient 驱动。
 * send() 只负责按 FIFO 顺序加入本地发送队列，断线时丢弃未发送数据，避免旧请求
 * 在新连接上被无条件重放。
 */
class QtNetworkTransport final : public INetworkTransport
{
    Q_OBJECT

public:
    QtNetworkTransport(const QString &host, quint16 port, QObject *parent = nullptr);
    /* 注入已创建的 socket，所有权转移给 transport。
     * 主要用于以可控 writeData() 验证部分写入；socket 必须与 transport 同线程。
     */
    QtNetworkTransport(const QString &host, quint16 port, QTcpSocket *socket,
                       QObject *parent);
    ~QtNetworkTransport() override;

    void connectToServer() override;
    void disconnectFromServer() override;
    bool send(const QByteArray &data) override;
    bool isConnected() const override;

private:
    bool flushWriteQueue();
    void clearWriteQueue();

    QString m_host;
    quint16 m_port;
    QTcpSocket *m_socket;
    QByteArray m_pendingWrite;
};
