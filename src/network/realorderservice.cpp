#include "realorderservice.h"

#include "backendclient.h"
#include "protocol.h"

#include <QDateTime>
#include <QTimer>

#include <algorithm>

namespace {

ClientError makeError(const QString &requestId, const QString &operationId,
                      const QString &code, const QString &message,
                      bool retryable, bool resultUnknown = false)
{
    // 订单查询为只读：永不携带 resultUnknown；operationId 仅透传（正常为空）。
    ClientError error;
    error.requestId = requestId;
    error.operationId = operationId;
    error.code = code;
    error.displayMessage = message;
    error.retryable = retryable;
    error.resultUnknown = resultUnknown;
    return error;
}

/** 服务端错误码中可重试的类型（数据库瞬态类）。 */
bool isRetryableServerError(int errType)
{
    return errType == DB_ERROR;
}

double numberValue(const QJsonValue &value, bool *ok)
{
    *ok = false;
    if (value.isDouble()) {
        *ok = true;
        return value.toDouble();
    }
    if (value.isString()) {
        bool parsed = false;
        const double number = value.toString().toDouble(&parsed);
        if (parsed) {
            *ok = true;
            return number;
        }
    }
    return 0.0;
}

QString recordString(const QJsonObject &record,
                     std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const QJsonValue value = record.value(QLatin1String(key));
        if (value.isString()) {
            return value.toString().trimmed();
        }
    }
    return QString();
}

QDateTime parseTimestamp(const QString &text)
{
    // 服务端时间格式未在协议文档明确：先按 ISO，再按空格分隔的常见
    // MySQL DATETIME 文本；解析失败返回无效 QDateTime，由调用方按缺失处理。
    if (text.isEmpty()) {
        return QDateTime();
    }
    const QDateTime iso = QDateTime::fromString(text, Qt::ISODate);
    if (iso.isValid()) {
        return iso;
    }
    return QDateTime::fromString(text, QStringLiteral("yyyy-MM-dd hh:mm:ss"));
}

} // namespace

RealOrderService::RealOrderService(BackendClient *backend, QObject *parent)
    : IOrderService(parent),
      m_backend(backend)
{
    connect(m_backend, &BackendClient::frameReceived,
            this, &RealOrderService::handleFrame);
    connect(m_backend, &BackendClient::connectionStateChanged,
            this, &RealOrderService::handleConnectionStateChanged);
}

void RealOrderService::setIdentity(const QString &username)
{
    // 登录成功注入 / 登出清空：orderInfo 以 username 为归属权威键。
    const QString normalized = username.trimmed();
    if (normalized == m_username) return;
    while (!m_queryQueue.isEmpty()) {
        const QueuedQuery queued = m_queryQueue.takeFirst();
        emitFailed(queued.context, QStringLiteral("identity-changed"),
                   QStringLiteral("登录账号已变化，已取消旧订单查询。"), false);
    }
    if (m_pending) {
        failPending(QStringLiteral("identity-changed"),
                    QStringLiteral("登录账号已变化，已取消旧订单查询。"), false);
    }
    m_username = normalized;
}

void RealOrderService::setRequestTimeoutMs(int timeoutMs)
{
    m_requestTimeoutMs = timeoutMs;
}

void RealOrderService::queryActiveOrder(const RequestContext &context)
{
    startQuery(QueryKind::ActiveOrder, context, QString());
}

void RealOrderService::queryActiveOrders(const RequestContext &context)
{
    startQuery(QueryKind::ActiveOrders, context, QString());
}

void RealOrderService::queryOrderHistory(const RequestContext &context)
{
    startQuery(QueryKind::OrderHistory, context, QString());
}

void RealOrderService::queryOrderDetail(const RequestContext &context,
                                        const QString &orderId)
{
    startQuery(QueryKind::OrderDetail, context, orderId);
}

void RealOrderService::stopCharging(const RequestContext &context,
                                    const QString &orderId)
{
    if (!context.isValid() || !context.isMutation() || orderId.trimmed().isEmpty()) {
        emitFailed(context, QStringLiteral("order-invalid-stop-request"),
                   QStringLiteral("停止充电参数无效。"), false);
        return;
    }
    if (m_username.isEmpty()) {
        emitFailed(context, QStringLiteral("order-identity-missing"),
                   QStringLiteral("当前用户身份未知。"), false);
        return;
    }
    if (m_backend->connectionState() != ConnectionState::Connected) {
        emitFailed(context, QStringLiteral("not-connected"),
                   QStringLiteral("服务器尚未连接。"), true);
        return;
    }
    if (m_pending) {
        emitFailed(context, QStringLiteral("request-in-flight"),
                   QStringLiteral("上一项订单请求仍在处理中。"), true);
        return;
    }

    PendingRequest pending;
    pending.kind = QueryKind::StopOrderLookup;
    pending.requestId = context.requestId;
    pending.operationId = context.operationId;
    pending.orderId = orderId.trimmed();
    pending.timer = new QTimer(this);
    pending.timer->setSingleShot(true);
    connect(pending.timer, &QTimer::timeout,
            this, &RealOrderService::handleTimeout);
    m_pending = pending;
    QJsonObject condition{{QStringLiteral("orderNo"), pending.orderId},
                          {QStringLiteral("username"), m_username}};
    if (!m_backend->sendFrame(ORDERQRY_REQ,
                              makeOrderQuery(condition, context.requestId))) {
        failPending(QStringLiteral("send-failed"),
                    QStringLiteral("停止前订单查询发送失败。"), true);
        return;
    }
    pending.timer->start(m_requestTimeoutMs);
}

void RealOrderService::queryStopResult(const RequestContext &context,
                                       const QString &operationId)
{
    if (!context.isValid() || context.isMutation() || operationId.trimmed().isEmpty()) {
        emitFailed(context, QStringLiteral("order-invalid-stop-result-query"),
                   QStringLiteral("停止结果查询参数无效。"), false);
        return;
    }
    if (m_username.isEmpty()) {
        emitFailed(context, QStringLiteral("order-identity-missing"),
                   QStringLiteral("当前用户身份未知。"), false);
        return;
    }
    if (m_pending) {
        emitFailed(context, QStringLiteral("request-in-flight"),
                   QStringLiteral("上一项订单请求仍在处理中。"), true);
        return;
    }
    PendingRequest pending;
    pending.kind = QueryKind::StopResult;
    pending.requestId = context.requestId;
    pending.operationId = operationId.trimmed();
    pending.timer = new QTimer(this);
    pending.timer->setSingleShot(true);
    connect(pending.timer, &QTimer::timeout,
            this, &RealOrderService::handleTimeout);
    m_pending = pending;
    QJsonObject condition{{QStringLiteral("username"), m_username},
                          {QStringLiteral("operationId"), pending.operationId}};
    if (!m_backend->sendFrame(ORDERQRY_REQ,
                              makeOrderQuery(condition, context.requestId))) {
        failPending(QStringLiteral("send-failed"),
                    QStringLiteral("停止结果查询发送失败。"), true);
        return;
    }
    pending.timer->start(m_requestTimeoutMs);
}

void RealOrderService::cancel(const QString &requestId)
{
    // 尽力取消：摘除在途关联即可，214 迟到应答因关联缺失被静默丢弃。
    if (m_pending && m_pending->requestId == requestId) {
        finishPending();
    }
    for (auto it = m_queryQueue.begin(); it != m_queryQueue.end();) {
        if (it->context.requestId == requestId)
            it = m_queryQueue.erase(it);
        else
            ++it;
    }
}

bool RealOrderService::startQuery(QueryKind kind, const RequestContext &context,
                                  const QString &orderId)
{
    if (!context.isValid()) {
        emitFailed(context, QStringLiteral("order-invalid-request"),
                   QStringLiteral("Request id is required."), false);
        return false;
    }
    if (context.isMutation()) {
        emitFailed(context, QStringLiteral("order-readonly-operation"),
                   QStringLiteral("Order queries must not contain an operation ID."),
                   false);
        return false;
    }
    if (!m_backend || m_backend->connectionState() != ConnectionState::Connected) {
        emitFailed(context, QStringLiteral("not-connected"),
                   QStringLiteral("Not connected to the server."), true);
        return false;
    }
    if (m_username.isEmpty()) {
        // 未登录/身份未注入：无法收敛查询范围，快速失败而不是查全表。
        emitFailed(context, QStringLiteral("order-identity-missing"),
                   QStringLiteral("Current user identity is unknown."), false);
        return false;
    }
    if (kind == QueryKind::OrderDetail && orderId.trimmed().isEmpty()) {
        emitFailed(context, QStringLiteral("order-invalid-request"),
                   QStringLiteral("Order id is required."), false);
        return false;
    }
    if (m_pending) {
        // 214 may not echo requestId. Preserve response attribution by
        // serialising read requests instead of rejecting UI refreshes.
        for (const QueuedQuery &queued : m_queryQueue) {
            if (queued.context.requestId == context.requestId) return true;
        }
        m_queryQueue.append({kind, context, orderId.trimmed()});
        return true;
    }

    PendingRequest pending;
    pending.kind = kind;
    pending.requestId = context.requestId;
    pending.operationId = context.operationId;
    pending.orderId = orderId.trimmed();
    pending.timer = new QTimer(this);
    pending.timer->setSingleShot(true);
    connect(pending.timer, &QTimer::timeout,
            this, &RealOrderService::handleTimeout);
    m_pending = pending;

    // 条件：活动订单按 username 拉取本用户全部订单后客户端筛选
    //（服务端 cond 对 status 支持未知，客户端过滤在"cond 被忽略返回全表"
    // 时依然正确）；详情按 orderNo 收敛，同样客户端过滤兜底。
    QJsonObject condition;
    if (kind == QueryKind::OrderDetail) {
        condition.insert(QStringLiteral("orderNo"), pending.orderId);
        condition.insert(QStringLiteral("username"), m_username);
    } else {
        condition.insert(QStringLiteral("username"), m_username);
    }
    if (!m_backend->sendFrame(ORDERQRY_REQ, makeOrderQuery(condition,
                                                           context.requestId))) {
        finishPending();
        emitFailed(context, QStringLiteral("send-failed"),
                   QStringLiteral("Failed to send the order query."), true);
        return false;
    }

    pending.timer->start(m_requestTimeoutMs);
    return true;
}

QJsonObject RealOrderService::makeOrderQuery(const QJsonObject &cond,
                                             const QString &requestId)
{
    // ORDERQRY_REQ {username?,stationName?,chargerCode?,orderNo?,status?}；
    // requestId 冗余携带，服务端忽略无害，未来支持回显时自动升级关联。
    QJsonObject payload = cond;
    payload.insert(QStringLiteral("requestId"), requestId);
    return payload;
}

void RealOrderService::handleFrame(int msgType, const QJsonObject &payload)
{
    if (!m_pending) {
        return; // 迟到应答：在途关联已摘除（超时/取消/断线）
    }
    // 服务端未来回显 requestId 时按值归属；当前 FIFO 单在途天然消歧。
    const QString echoed = payload.value(QStringLiteral("requestId")).toString();
    if (!echoed.isEmpty() && echoed != m_pending->requestId) {
        return;
    }

    if (msgType == ORDERQRY_ACK) {
        QJsonValue data = payload.value(QStringLiteral("orders"));
        if (!data.isArray()) data = payload.value(QStringLiteral("data"));
        if (!data.isArray()) data = payload.value(QStringLiteral("rows"));
        if (!data.isArray()) {
            // 兼容部分实现返回 data 包装的 JSON 数组。
            const QJsonValue wrapped = payload.value(QStringLiteral("data"));
            if (wrapped.isArray()) {
                handleFrame(ORDERQRY_ACK, QJsonObject{
                    {QStringLiteral("orders"), wrapped}});
                return;
            }
            failPending(QStringLiteral("bad-response"),
                        QStringLiteral("Server response was malformed."), true);
            return;
        }

        if (m_pending->kind == QueryKind::StopOrderLookup) {
            ChargingOrder matched;
            bool found = false;
            for (const QJsonValue &value : data.toArray()) {
                if (!value.isObject()) continue;
                const ChargingOrder order = parseOrderRecord(value.toObject());
                if (order.orderId == m_pending->orderId) {
                    matched = order;
                    found = true;
                    break;
                }
            }
            if (!found || matched.chargerCode.isEmpty()) {
                failPending(QStringLiteral("order-not-found"),
                            QStringLiteral("未找到可停止的订单或电桩。"), false);
                return;
            }
            m_pending->chargerCode = matched.chargerCode;
            m_pending->kind = QueryKind::StopRequest;
            QJsonObject stopPayload{
                {QStringLiteral("chargerCode"), matched.chargerCode},
                {QStringLiteral("username"), m_username},
                {QStringLiteral("requestId"), m_pending->requestId},
                {QStringLiteral("operationId"), m_pending->operationId}};
            if (!m_backend->sendFrame(STOP_CHARGING_REQ, stopPayload)) {
                failPending(QStringLiteral("send-failed"),
                            QStringLiteral("停止充电请求发送失败。"), false, true);
            }
            return;
        }

        const PendingRequest pending = *m_pending;
        finishPending();

        const RequestContext context{pending.requestId, pending.operationId};

        if (pending.kind == QueryKind::OrderDetail) {
            // 详情：orderNo 精确匹配；找不到视为数据不存在而非解析失败。
            ChargingOrder matched;
            bool found = false;
            for (const QJsonValue &value : data.toArray()) {
                if (!value.isObject()) {
                    continue;
                }
                const ChargingOrder order = parseOrderRecord(value.toObject());
                if (order.orderId == pending.orderId) {
                    matched = order;
                    found = true;
                    break;
                }
            }
            if (!found) {
                emitFailed(context, QStringLiteral("order-not-found"),
                           QStringLiteral("Requested order does not exist."),
                           false);
                return;
            }
            emit orderDetailReady(context, matched);
            return;
        }

        if (pending.kind == QueryKind::StopResult) {
            ChargingOrder matched;
            bool found = false;
            for (const QJsonValue &value : data.toArray()) {
                if (!value.isObject()) continue;
                const QJsonObject record = value.toObject();
                if (record.value(QStringLiteral("operationId")).toString()
                    != pending.operationId) continue;
                matched = parseOrderRecord(record);
                found = true;
                break;
            }
            StopOperationStatus status;
            status.requestId = pending.requestId;
            status.operationId = pending.operationId;
            if (found && (matched.status == OrderStatus::PendingSettlement
                          || matched.status == OrderStatus::Settled)) {
                status.state = StopOperationState::Succeeded;
                status.result = StopChargingResult{pending.requestId,
                                                   pending.operationId,
                                                   matched};
            } else {
                status.state = StopOperationState::Pending;
            }
            emit stopOperationStatusReady(RequestContext{pending.requestId,
                                                          pending.operationId},
                                          status);
            return;
        }

        if (pending.kind == QueryKind::ActiveOrders) {
            QVector<ChargingOrder> activeOrders;
            for (const QJsonValue &value : data.toArray()) {
                if (!value.isObject()) continue;
                const ChargingOrder order = parseOrderRecord(value.toObject());
                if (order.status == OrderStatus::Charging
                    || order.status == OrderStatus::PendingSettlement) {
                    activeOrders.append(order);
                }
            }
            emit activeOrdersReady(context, activeOrders);
            return;
        }

        if (pending.kind == QueryKind::OrderHistory) {
            QVector<ChargingOrder> history;
            for (const QJsonValue &value : data.toArray()) {
                if (!value.isObject()) continue;
                const ChargingOrder order = parseOrderRecord(value.toObject());
                if (!order.orderId.isEmpty()) history.append(order);
            }
            emit orderHistoryReady(context, history);
            return;
        }

        // 活动订单：Charging 优先于 PendingSettlement，最多返回一条。
        std::optional<ChargingOrder> active;
        for (const QJsonValue &value : data.toArray()) {
            if (!value.isObject()) {
                continue;
            }
            const ChargingOrder order = parseOrderRecord(value.toObject());
            if (order.status != OrderStatus::Charging
                && order.status != OrderStatus::PendingSettlement) {
                continue;
            }
            if (!active.has_value()
                || (order.status == OrderStatus::Charging
                    && active->status != OrderStatus::Charging)) {
                active = order;
            }
        }
        emit activeOrderReady(context, active);
        return;
    }

    if (msgType == STOP_CHARGING_ACK
        && m_pending->kind == QueryKind::StopRequest) {
        const PendingRequest pending = *m_pending;
        ChargingOrder order = parseOrderRecord(payload);
        if (order.orderId.isEmpty()) order.orderId = pending.orderId;
        if (order.chargerCode.isEmpty()) order.chargerCode = pending.chargerCode;
        if (order.chargerId.isEmpty()) order.chargerId = order.chargerCode;
        order.status = OrderStatus::PendingSettlement;
        const StopChargingResult result{pending.requestId, pending.operationId, order};
        finishPending();
        emit chargingStopped(RequestContext{pending.requestId, pending.operationId}, result);
        return;
    }

    if (msgType == ILLEGAL_REQUEST || msgType == DATA_NOEXIST
        || msgType == DB_ERROR || msgType == PARAM_ERROR
        || msgType == LOGIN_FAIL || msgType == DATA_EXIST) {
        // 服务端错误规范：code 字符串优先，err/reason 仅作展示补充。
        const QString bizCode = payload.value(QStringLiteral("code")).toString();
        QString reason = payload.value(QStringLiteral("err")).toString();
        if (reason.isEmpty()) {
            reason = payload.value(QStringLiteral("reason")).toString();
        }
        const int errType = msgType;
        const PendingRequest pending = *m_pending;
        finishPending();
        ClientError error;
        error.requestId = pending.requestId;
        error.operationId = pending.operationId;
        error.code = bizCode.isEmpty()
                         ? QStringLiteral("server-error-%1").arg(errType)
                         : bizCode;
        error.displayMessage =
            reason.isEmpty()
                ? QStringLiteral("Server rejected the order query (%1).").arg(errType)
                : reason;
        error.retryable = isRetryableServerError(errType);
        emit requestFailed(error);
    }
}

ChargingOrder RealOrderService::parseOrderRecord(const QJsonObject &record)
{
    // 坏行不丢弃整批：orderId 缺失的记录在详情匹配/活动筛选中自然无法命中。
    ChargingOrder order;
    order.orderId = recordString(record, {"orderNo", "orderId", "order_id", "id"});
    order.stationName = recordString(record, {"stationName", "station", "stationId"});
    order.chargerCode = recordString(record, {"chargerCode", "charger", "chargerId"});
    order.stationId = order.stationName; // 稳定 ID 待 v2.5 冲突解决，暂用名称
    order.chargerId = order.chargerCode;

    const QString status = recordString(record, {"status"}).trimmed().toLower();
    if (status == QLatin1String("charging") || status == QLatin1String("in_progress")) {
        order.status = OrderStatus::Charging;
    } else if (status == QLatin1String("pendingsettlement")
               || status == QLatin1String("pending_settlement")
               || status == QLatin1String("pending_payment")) {
        order.status = OrderStatus::PendingSettlement;
    } else if (status == QLatin1String("settled") || status == QLatin1String("completed")) {
        order.status = OrderStatus::Settled;
    } else if (status == QLatin1String("cancelled") || status == QLatin1String("canceled")) {
        order.status = OrderStatus::Cancelled;
    } else {
        order.status = OrderStatus::Unknown;
    }

    bool ok = false;
    const double priceCents = numberValue(record.value(QStringLiteral("priceCents")), &ok);
    if (ok) {
        order.priceCentsPerKwhSnapshot = static_cast<qint64>(priceCents);
    } else {
        const double yuan = numberValue(record.value(QStringLiteral("price")), &ok);
        if (ok && yuan >= 0.0) {
            order.priceCentsPerKwhSnapshot = static_cast<qint64>(qRound64(yuan * 100.0));
        }
    }

    const QJsonValue kwhValue = record.contains(QStringLiteral("kwh"))
                                    ? record.value(QStringLiteral("kwh"))
                                    : record.value(QStringLiteral("energyKwh"));
    const double kwh = numberValue(kwhValue, &ok);
    if (ok && kwh >= 0.0) {
        order.energyKwh = kwh;
    }

    const double amountCents = numberValue(
        record.value(QStringLiteral("amountCents")), &ok);
    if (ok && amountCents >= 0.0) {
        order.amountCents = static_cast<qint64>(qRound64(amountCents));
    } else {
        const double amount = numberValue(record.value(QStringLiteral("amount")), &ok);
        if (ok && amount >= 0.0) {
            order.amountCents = static_cast<qint64>(qRound64(amount * 100.0));
        }
    }
    // 金额和电量均以服务端持久化值为准，客户端禁止自行计费。

    order.startedAtUtc = parseTimestamp(recordString(record, {"startedAt", "startTime"}));
    const QDateTime ended = parseTimestamp(recordString(record, {"endedAt", "endTime"}));
    if (ended.isValid()) {
        order.endedAtUtc = ended;
    }
    const QDateTime deadline =
        parseTimestamp(recordString(record, {"paymentDeadline", "payDeadline"}));
    if (deadline.isValid()) {
        order.paymentDeadlineUtc = deadline;
    }

    return order;
}

void RealOrderService::handleConnectionStateChanged(ConnectionState state)
{
    // 断线/重连中立即失败在途查询并允许重试：比等超时兜底更快可恢复。
    if ((state == ConnectionState::Disconnected
         || state == ConnectionState::Reconnecting)
        && m_pending) {
        failAllPending(QStringLiteral("connection-lost"),
                       QStringLiteral("Connection to server was lost."));
    }
}

void RealOrderService::handleTimeout()
{
    if (!m_pending) {
        return;
    }
    const bool mutation = m_pending->kind == QueryKind::StopOrderLookup
                          || m_pending->kind == QueryKind::StopRequest;
    failPending(QStringLiteral("request-timeout"),
                mutation ? QStringLiteral("停止结果未知，请查询原订单状态，勿重复停止。")
                         : QStringLiteral("Request timed out."),
                !mutation, mutation);
}

void RealOrderService::finishPending()
{
    if (!m_pending) return;
    if (m_pending->timer) {
        m_pending->timer->stop();
        m_pending->timer->deleteLater();
    }
    m_pending.reset();
    QTimer::singleShot(0, this, &RealOrderService::startNextQueuedQuery);
}

void RealOrderService::startNextQueuedQuery()
{
    if (m_pending || m_queryQueue.isEmpty()) return;
    const QueuedQuery queued = m_queryQueue.takeFirst();
    startQuery(queued.kind, queued.context, queued.orderId);
}

void RealOrderService::failPending(const QString &code, const QString &message,
                                   bool retryable, bool resultUnknown)
{
    if (!m_pending) {
        return;
    }
    const PendingRequest pending = *m_pending;
    finishPending();
    emit requestFailed(makeError(pending.requestId, pending.operationId,
                                 code, message, retryable, resultUnknown));
}

void RealOrderService::failAllPending(const QString &code, const QString &message)
{
    while (m_pending) {
        const bool mutation = m_pending->kind == QueryKind::StopOrderLookup
                              || m_pending->kind == QueryKind::StopRequest;
        failPending(code, message, !mutation, mutation);
    }
}

void RealOrderService::emitFailed(const RequestContext &context, const QString &code,
                                  const QString &message, bool retryable)
{
    emit requestFailed(makeError(context.requestId, context.operationId,
                                 code, message, retryable));
}
