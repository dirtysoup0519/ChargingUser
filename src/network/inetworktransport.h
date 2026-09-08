#pragma once

#include <QObject>
#include <QByteArray>

/* 网络传输抽象（合同 §7 步骤 7）：
 * BackendClient 只依赖本接口，不直接接触 QTcpSocket，
 * 使连接管理与编解码可在无 socket 的测试环境中验证。
 * 线程约定：创建、调用与信号全部发生在对象所属线程；正式程序将实现放在
 * ClientSocketWorker 的专用网络线程，测试可在测试线程内直接使用。
 */
class INetworkTransport : public QObject
{
    Q_OBJECT

public:
    explicit INetworkTransport(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~INetworkTransport() override = default;

    virtual void connectToServer() = 0;
    virtual void disconnectFromServer() = 0;

    // 未连接或发送失败返回 false；成功入队返回 true
    virtual bool send(const QByteArray &data) = 0;
    virtual bool isConnected() const = 0;

signals:
    void connected();
    void disconnected();
    void dataReceived(const QByteArray &data);
    void transportError(const QString &message);
};
