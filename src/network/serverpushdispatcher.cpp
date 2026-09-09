#include "serverpushdispatcher.h"

#include "backendclient.h"
#include "protocol.h"

#include <QtMath>

namespace {

qint64 parseAmountCents(const QJsonObject &payload, bool *present)
{
    *present = false;
    const QJsonValue cents = payload.value(QStringLiteral("amountCents"));
    if (cents.isDouble()) {
        *present = true;
        return qRound64(cents.toDouble());
    }
    if (cents.isString()) {
        bool ok = false;
        const qint64 value = cents.toString().trimmed().toLongLong(&ok);
        if (ok) {
            *present = true;
            return value;
        }
    }
    const QJsonValue yuan = payload.value(QStringLiteral("amount"));
    if (yuan.isDouble()) {
        *present = true;
        return qRound64(yuan.toDouble() * 100.0);
    }
    if (yuan.isString()) {
        bool ok = false;
        const double value = yuan.toString().trimmed().toDouble(&ok);
        if (ok) {
            *present = true;
            return qRound64(value * 100.0);
        }
    }
    return 0;
}
}

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
        if (notice.orderId.isEmpty())
            notice.orderId = payload.value(QStringLiteral("orderId")).toString();
        notice.chargerCode = payload.value(QStringLiteral("chargerCode")).toString();
        notice.energyKwh = payload.value(QStringLiteral("kwh")).toDouble();
        notice.amountCents = parseAmountCents(payload, &notice.amountPresent);
        notice.amountYuan = payload.value(QStringLiteral("amount")).toDouble();
        notice.percent = payload.value(QStringLiteral("percent")).toInt();
        notice.remainMinutes = payload.value(QStringLiteral("remainMin")).toInt();
        if (!notice.orderId.isEmpty()) emit chargingProgress(notice);
        break;
    }
    case CHG_FAULT_NOTICE: {
        ChargingFaultNotice notice;
        notice.orderId = payload.value(QStringLiteral("orderNo")).toString();
        if (notice.orderId.isEmpty())
            notice.orderId = payload.value(QStringLiteral("orderId")).toString();
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
