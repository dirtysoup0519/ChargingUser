#pragma once

#include "common/connectionstate.h"

#include <QJsonObject>
#include <QObject>

/**
 * 业务适配器使用的异步后端通道。
 *
 * 实现可以在独立线程中管理 socket，但调用方始终在自己的线程中通过信号槽收发。
 * 本接口只表达连接与协议帧，不解释任何用户、站点、订单或钱包字段。
 */
class IBackendChannel : public QObject
{
    Q_OBJECT

public:
    explicit IBackendChannel(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~IBackendChannel() override = default;

    virtual ConnectionState connectionState() const = 0;

public slots:
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void sendFrame(int messageType,
                           const QJsonObject &payload = QJsonObject()) = 0;

signals:
    void frameReceived(int messageType, const QJsonObject &payload);
    void frameSendFailed(int messageType, const QString &message);
    void connectionStateChanged(ConnectionState state);
    void networkError(const QString &message);
};

