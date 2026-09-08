#include "serverpushdispatcher.h"

#include "backendclient.h"
#include "protocol.h"

ServerPushDispatcher::ServerPushDispatcher(BackendClient *backend,
                                           QObject *parent)
    : QObject(parent), m_backend(backend)
{
    Q_ASSERT(m_backend);
    qRegisterMetaType<ChargingProgressNotice>();
    qRegisterMetaType<ChargingFaultNotice>();
    connect(m_backend, &BackendClient::frameReceived,
            this, &ServerPushDispatcher::handleFrame);
}

void ServerPushDispatcher::setIdentity(const QString &username)
{
    m_username = username.trimmed();
}

void ServerPushDispatcher::handleFrame(int msgType, const QJsonObject &payload)
{
    if (!belongsToCurrentUser(payload)) return;

    switch (msgType) {
    case PHONE_LOGIN_ACK:
    case RECHARGE_ACK:
    case PAY_ACK:
        emit balanceChanged();
        break;
    case PAYMENT_NOTICE:
        emit paymentNotice(payload.value(QStringLiteral("orderNo")).toString());
        break;
    case CHG_PROGRESS: {
        ChargingProgressNotice notice;
        notice.orderId = payload.value(QStringLiteral("orderNo")).toString();
        notice.chargerCode = payload.value(QStringLiteral("chargerCode")).toString();
        notice.energyKwh = payload.value(QStringLiteral("kwh")).toDouble();
        notice.amountCents = payload.value(QStringLiteral("amountCents")).toVariant()
                                .toLongLong();
        notice.percent = payload.value(QStringLiteral("percent")).toInt();
        notice.remainMinutes = payload.value(QStringLiteral("remainMin")).toInt();
        if (!notice.orderId.isEmpty()) emit chargingProgress(notice);
        break;
    }
    case CHG_FAULT_NOTICE: {
        ChargingFaultNotice notice;
        notice.orderId = payload.value(QStringLiteral("orderNo")).toString();
        notice.chargerCode = payload.value(QStringLiteral("chargerCode")).toString();
        notice.reason = payload.value(QStringLiteral("reason")).toString();
        notice.settled = payload.value(QStringLiteral("settled")).toBool();
        emit chargingFault(notice);
        break;
    }
    case RESERVE_EXPIRED_NOTICE:
        emit reservationExpired(
            payload.value(QStringLiteral("reserveId")).toString(),
            payload.value(QStringLiteral("chargerCode")).toString());
        break;
    default:
        break;
    }
}

bool ServerPushDispatcher::belongsToCurrentUser(const QJsonObject &payload) const
{
    const QString username = payload.value(QStringLiteral("username")).toString().trimmed();
    return username.isEmpty() || (!m_username.isEmpty() && username == m_username);
}
