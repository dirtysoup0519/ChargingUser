#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QObject>

class BackendClient;

struct ChargingProgressNotice
{
    QString orderId;
    QString chargerCode;
    double energyKwh = 0.0;
    qint64 amountCents = 0;
    int percent = 0;
    int remainMinutes = 0;
};

struct ChargingFaultNotice
{
    QString orderId;
    QString chargerCode;
    QString reason;
    bool settled = false;
};

class ServerPushDispatcher final : public QObject
{
    Q_OBJECT
public:
    explicit ServerPushDispatcher(BackendClient *backend,
                                  QObject *parent = nullptr);
    void setIdentity(const QString &username);

signals:
    void balanceChanged();
    void paymentNotice(const QString &orderId);
    void chargingProgress(const ChargingProgressNotice &notice);
    void chargingFault(const ChargingFaultNotice &notice);
    void reservationExpired(const QString &reservationId,
                            const QString &chargerCode);

private slots:
    void handleFrame(int msgType, const QJsonObject &payload);

private:
    bool belongsToCurrentUser(const QJsonObject &payload) const;
    BackendClient *m_backend;
    QString m_username;
};

Q_DECLARE_METATYPE(ChargingProgressNotice)
Q_DECLARE_METATYPE(ChargingFaultNotice)
