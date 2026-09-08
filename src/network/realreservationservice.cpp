#include "realreservationservice.h"

#include "backendclient.h"
#include "protocol.h"

#include <QDateTime>
#include <QTimer>

RealReservationService::RealReservationService(BackendClient *backend,
                                               QObject *parent)
    : IReservationService(parent), m_backend(backend)
{
    Q_ASSERT(m_backend);
    connect(m_backend, &BackendClient::frameReceived,
            this, &RealReservationService::handleFrame);
    connect(m_backend, &BackendClient::connectionStateChanged,
            this, &RealReservationService::handleConnectionStateChanged);
}

void RealReservationService::setIdentity(const QString &username)
{
    m_username = username.trimmed();
}

void RealReservationService::setRequestTimeoutMs(int timeoutMs)
{
    m_requestTimeoutMs = qMax(1, timeoutMs);
}

void RealReservationService::reserve(const RequestContext &context,
                                      const QString &stationId,
                                      const QString &chargerId,
                                      int durationSeconds)
{
    if (!context.isValid() || !context.isMutation() || stationId.trimmed().isEmpty()
        || chargerId.trimmed().isEmpty() || durationSeconds <= 0) {
        emitFailure(context, QStringLiteral("reservation-invalid-request"),
                    QStringLiteral("预约参数无效。"));
        return;
    }
    if (m_username.isEmpty()) {
        emitFailure(context, QStringLiteral("reservation-no-identity"),
                    QStringLiteral("请先登录。"));
        return;
    }
    if (m_backend->connectionState() != ConnectionState::Connected) {
        emitFailure(context, QStringLiteral("not-connected"),
                    QStringLiteral("服务器尚未连接。"), true);
        return;
    }
    if (m_pending) {
        emitFailure(context, QStringLiteral("reservation-request-in-flight"),
                    QStringLiteral("上一项预约仍在处理中。"), true);
        return;
    }
    PendingRequest pending;
    pending.context = context;
    pending.stationId = stationId.trimmed();
    pending.chargerId = chargerId.trimmed();
    pending.timer = new QTimer(this);
    pending.timer->setSingleShot(true);
    connect(pending.timer, &QTimer::timeout,
            this, &RealReservationService::handleTimeout);
    m_pending = pending;
    QJsonObject payload{{QStringLiteral("chargerCode"), pending.chargerId},
                        {QStringLiteral("username"), m_username},
                        {QStringLiteral("durationSeconds"), durationSeconds},
                        {QStringLiteral("requestId"), context.requestId},
                        {QStringLiteral("operationId"), context.operationId}};
    if (!m_backend->sendFrame(RESERVE_REQ, payload)) {
        failPending(QStringLiteral("send-failed"),
                    QStringLiteral("预约请求发送失败。"), false, true);
        return;
    }
    m_pending->timer->start(m_requestTimeoutMs);
}

void RealReservationService::cancel(const QString &requestId)
{
    if (m_pending && m_pending->context.requestId == requestId) finishPending();
}

void RealReservationService::handleFrame(int msgType, const QJsonObject &payload)
{
    if (!m_pending) return;
    const QString echoed = payload.value(QStringLiteral("requestId")).toString();
    if (!echoed.isEmpty() && echoed != m_pending->context.requestId) return;
    if (msgType == RESERVE_ACK) {
        const PendingRequest pending = *m_pending;
        ReservationResult result;
        result.reservationId = payload.value(QStringLiteral("reserveId")).toString();
        result.stationId = pending.stationId;
        result.chargerId = pending.chargerId;
        result.chargerCode = payload.value(QStringLiteral("chargerCode"))
                                 .toString(pending.chargerId);
        result.reservedAtUtc = QDateTime::fromString(
            payload.value(QStringLiteral("reserveAt")).toString(), Qt::ISODate);
        result.expiresAtUtc = QDateTime::fromString(
            payload.value(QStringLiteral("expireAt")).toString(), Qt::ISODate);
        result.balanceCents = payload.value(QStringLiteral("balanceCents")).toVariant()
                                  .toLongLong();
        finishPending();
        emit reservationCreated(pending.context, result);
        return;
    }
    if (msgType >= 300 && msgType < 400) {
        failPending(payload.value(QStringLiteral("code")).toString(
                        QStringLiteral("reservation-server-error")),
                    payload.value(QStringLiteral("reason")).toString(
                        QStringLiteral("服务器拒绝了预约请求。")),
                    msgType == DB_ERROR);
    }
}

void RealReservationService::handleConnectionStateChanged(ConnectionState state)
{
    if (state != ConnectionState::Connected && m_pending) {
        failPending(QStringLiteral("connection-lost"),
                    QStringLiteral("服务器连接已断开。"), false, true);
    }
}

void RealReservationService::handleTimeout()
{
    failPending(QStringLiteral("request-timeout"),
                QStringLiteral("预约结果未知，请查询预约状态，勿重复提交。"),
                false, true);
}

void RealReservationService::finishPending()
{
    if (!m_pending) return;
    if (m_pending->timer) {
        m_pending->timer->stop();
        m_pending->timer->deleteLater();
    }
    m_pending.reset();
}

void RealReservationService::failPending(const QString &code,
                                         const QString &message,
                                         bool retryable, bool resultUnknown)
{
    if (!m_pending) return;
    const RequestContext context = m_pending->context;
    finishPending();
    emitFailure(context, code, message, retryable, resultUnknown);
}

void RealReservationService::emitFailure(const RequestContext &context,
                                         const QString &code,
                                         const QString &message,
                                         bool retryable, bool resultUnknown)
{
    ClientError error;
    error.requestId = context.requestId;
    error.operationId = context.operationId;
    error.code = code;
    error.displayMessage = message;
    error.retryable = retryable;
    error.resultUnknown = resultUnknown;
    emit requestFailed(error);
}
