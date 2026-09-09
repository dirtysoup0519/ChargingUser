#include "realreservationservice.h"

#include "backendclient.h"
#include "protocol.h"

#include <QDateTime>
#include <QJsonArray>
#include <QTimer>
#include <QtMath>

#include <algorithm>

namespace {
QDateTime parseTimestamp(const QJsonValue &value)
{
    if (!value.isString()) return {};
    const QString text = value.toString();
    QDateTime result = QDateTime::fromString(text, Qt::ISODate);
    if (!result.isValid())
        result = QDateTime::fromString(text, QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    return result;
}

qint64 cents(const QJsonObject &row, const QString &key, const QString &yuanKey)
{
    bool ok = false;
    const qint64 value = row.value(key).toVariant().toLongLong(&ok);
    if (ok) return value;
    const double yuan = row.value(yuanKey).toVariant().toDouble(&ok);
    return ok ? qRound64(yuan * 100.0) : 0;
}
}

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
    Q_UNUSED(durationSeconds); // 服务端按协议固定预约时长为 2 小时。
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
    // v2.6: 服务端按会话身份识别用户，125 业务载荷只要求 chargerCode。
    QJsonObject payload{{QStringLiteral("chargerCode"), pending.chargerId},
                        {QStringLiteral("requestId"), context.requestId}};
    if (!m_backend->sendFrame(RESERVE_REQ, payload)) {
        failPending(QStringLiteral("send-failed"),
                    QStringLiteral("预约请求发送失败。"), false, true);
        return;
    }
    m_pending->timer->start(m_requestTimeoutMs);
}

void RealReservationService::cancel(const QString &requestId)
{
    if (m_pending && m_pending->context.requestId == requestId) {
        // 摘除连接层可能残留的预约历史 GETDATA，避免阻塞后续查询。
        m_backend->cancelQuery(requestId);
        finishPending();
    }
}

void RealReservationService::cancelReservation(const RequestContext &context)
{
    if (!context.isValid() || !context.isMutation()) {
        emitFailure(context, QStringLiteral("reservation-invalid-cancel-request"),
                    QStringLiteral("取消预约参数无效。"));
        return;
    }
    if (m_username.isEmpty()) {
        emitFailure(context, QStringLiteral("reservation-no-identity"), QStringLiteral("请先登录。"));
        return;
    }
    if (m_backend->connectionState() != ConnectionState::Connected) {
        emitFailure(context, QStringLiteral("not-connected"), QStringLiteral("服务器尚未连接。"), true);
        return;
    }
    if (m_pending) {
        emitFailure(context, QStringLiteral("reservation-request-in-flight"), QStringLiteral("上一项预约仍在处理中。"), true);
        return;
    }
    PendingRequest pending;
    pending.context = context;
    pending.cancellation = true;
    pending.timer = new QTimer(this);
    pending.timer->setSingleShot(true);
    connect(pending.timer, &QTimer::timeout, this, &RealReservationService::handleTimeout);
    m_pending = pending;
    // v2.6.5: 126 载荷可为空；用户身份和 Active 预约由服务端会话确定。
    QJsonObject payload;
    if (!m_backend->sendFrame(CANCEL_RESERVE_REQ, payload)) {
        failPending(QStringLiteral("send-failed"), QStringLiteral("取消预约请求发送失败。"), false, true);
        return;
    }
    m_pending->timer->start(m_requestTimeoutMs);
}

void RealReservationService::queryHistory(const RequestContext &context)
{
    if (!context.isValid() || context.isMutation() || m_username.isEmpty()
        || m_pending || m_backend->connectionState() != ConnectionState::Connected) {
        emitFailure(context, QStringLiteral("reservation-query-unavailable"),
                    QStringLiteral("预约记录暂时无法查询。"), true);
        return;
    }
    PendingRequest pending;
    pending.context = context;
    pending.historyQuery = true;
    pending.timer = new QTimer(this);
    pending.timer->setSingleShot(true);
    connect(pending.timer, &QTimer::timeout, this, &RealReservationService::handleTimeout);
    m_pending = pending;
    QJsonObject cond{{QStringLiteral("username"), m_username}};
    QJsonObject payload{{QStringLiteral("table"), QStringLiteral("reservation")},
                        {QStringLiteral("cond"), cond},
                        {QStringLiteral("requestId"), context.requestId}};
    if (!m_backend->sendFrame(GETDATA, payload)) {
        failPending(QStringLiteral("send-failed"), QStringLiteral("预约记录查询发送失败。"), true);
        return;
    }
    m_pending->timer->start(m_requestTimeoutMs);
}

void RealReservationService::handleFrame(int msgType, const QJsonObject &payload)
{
    if (!m_pending) return;
    const QString echoed = payload.value(QStringLiteral("requestId")).toString();
    if (!echoed.isEmpty() && echoed != m_pending->context.requestId) return;
    if (msgType == DATA && m_pending->historyQuery) {
        QJsonValue data = payload.value(QStringLiteral("data"));
        if (!data.isArray()) data = payload.value(QStringLiteral("rows"));
        if (!data.isArray()) data = payload.value(QStringLiteral("records"));
        if (!data.isArray()) {
            failPending(QStringLiteral("bad-response"),
                        QStringLiteral("预约记录响应格式错误。"), true);
            return;
        }
        const QJsonArray rows = data.toArray();
        const PendingRequest pending = *m_pending;
        QVector<ReservationHistoryItem> items;
        for (const QJsonValue &value : rows) {
            if (!value.isObject()) continue;
            const QJsonObject row = value.toObject();
            const QString rowUser = row.value(QStringLiteral("username"))
                                        .toString().trimmed();
            if (!rowUser.isEmpty() && rowUser != m_username) continue;
            ReservationHistoryItem item;
            item.reservationId = row.value(QStringLiteral("id")).toVariant().toString();
            if (item.reservationId.isEmpty())
                item.reservationId = row.value(QStringLiteral("reserveId"))
                                         .toVariant().toString();
            item.stationName = row.value(QStringLiteral("stationName")).toString();
            item.chargerCode = row.value(QStringLiteral("chargerCode")).toString();
            item.depositCents = cents(row, QStringLiteral("depositCents"),
                                      QStringLiteral("deposit"));
            item.status = row.value(QStringLiteral("status")).toString();
            item.createdAtUtc = parseTimestamp(row.value(QStringLiteral("createdAt")));
            item.reserveAtUtc = parseTimestamp(row.value(QStringLiteral("reserveAt")));
            if (!item.reservationId.isEmpty()) items.append(item);
        }
        std::sort(items.begin(), items.end(),
                  [](const ReservationHistoryItem &left,
                     const ReservationHistoryItem &right) {
            const QDateTime leftTime = left.reserveAtUtc.isValid()
                                           ? left.reserveAtUtc : left.createdAtUtc;
            const QDateTime rightTime = right.reserveAtUtc.isValid()
                                            ? right.reserveAtUtc : right.createdAtUtc;
            return leftTime > rightTime;
        });
        finishPending();
        emit reservationHistoryReady(pending.context, items);
        return;
    }
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
    if (msgType == CANCEL_RESERVE_ACK && m_pending->cancellation) {
        const PendingRequest pending = *m_pending;
        ReservationCancellationResult result;
        result.reservationId = payload.value(QStringLiteral("reserveId")).toString();
        result.chargerCode = payload.value(QStringLiteral("chargerCode")).toString();
        result.refundCents = payload.value(QStringLiteral("refundCents")).toVariant().toLongLong();
        result.balanceCents = payload.value(QStringLiteral("balanceCents")).toVariant().toLongLong();
        finishPending();
        emit reservationCancelled(pending.context, result);
        return;
    }
    if (msgType == DATA_NOEXIST && m_pending->historyQuery) {
        const PendingRequest pending = *m_pending;
        finishPending();
        emit reservationHistoryReady(pending.context, {});
        return;
    }
    if (msgType >= 300 && msgType < 400) {
        QString message = payload.value(QStringLiteral("err")).toString();
        if (message.isEmpty()) message = payload.value(QStringLiteral("reason")).toString();
        if (message.isEmpty()) message = QStringLiteral("服务器拒绝了预约请求。");
        failPending(payload.value(QStringLiteral("code")).toString(
                        QStringLiteral("reservation-server-error")),
                    message,
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
    if (!m_pending) return;
    // 历史查询超时是只读失败；变更操作超时保持结果未知纪律。
    if (m_pending->historyQuery) {
        m_backend->cancelQuery(m_pending->context.requestId);
        failPending(QStringLiteral("request-timeout"),
                    QStringLiteral("预约历史查询超时，请稍后重试。"), true);
        return;
    }
    m_backend->cancelQuery(m_pending->context.requestId);
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
