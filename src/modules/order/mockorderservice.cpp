#include "modules/order/mockorderservice.h"

#include <QTimer>
#include <QtMath>

MockOrderService::MockOrderService(QObject *parent) : IOrderService(parent) {}

void MockOrderService::setOrders(const QVector<ChargingOrder> &orders)
{
    m_orders.clear();
    for (const ChargingOrder &order : orders)
        if (!order.orderId.isEmpty()) m_orders.insert(order.orderId, order);
}

void MockOrderService::upsertOrder(const ChargingOrder &order)
{
    if (!order.orderId.isEmpty()) m_orders.insert(order.orderId, order);
}

void MockOrderService::markSettled(const QString &orderId)
{
    if (!m_orders.contains(orderId)) return;
    ChargingOrder order = m_orders.value(orderId);
    order.status = OrderStatus::Settled;
    m_orders.insert(orderId, order);
}

void MockOrderService::queryActiveOrder(const RequestContext &context)
{
    if (!context.isValid() || context.isMutation()) {
        fail(context, QStringLiteral("order-invalid-query"),
             QStringLiteral("活动订单查询参数无效。"));
        return;
    }
    QTimer::singleShot(0, this, [this, context] {
        if (m_cancelled.remove(context.requestId)) return;
        for (const ChargingOrder &order : m_orders) {
            if (order.status == OrderStatus::Charging) {
                emit activeOrderReady(context, order);
                return;
            }
        }
        emit activeOrderReady(context, std::nullopt);
    });
}

void MockOrderService::queryActiveOrders(const RequestContext &context)
{
    if (!context.isValid() || context.isMutation()) {
        fail(context, QStringLiteral("order-invalid-query"),
             QStringLiteral("活动订单集合查询参数无效。"));
        return;
    }
    QTimer::singleShot(0, this, [this, context] {
        if (m_cancelled.remove(context.requestId)) return;
        QVector<ChargingOrder> activeOrders;
        for (const ChargingOrder &order : m_orders) {
            if (order.status == OrderStatus::Charging)
                activeOrders.append(order);
        }
        emit activeOrdersReady(context, activeOrders);
    });
}

void MockOrderService::queryOrderHistory(const RequestContext &context)
{
    if (!context.isValid() || context.isMutation()) {
        fail(context, QStringLiteral("order-invalid-query"),
             QStringLiteral("订单历史查询参数无效。"));
        return;
    }
    QTimer::singleShot(0, this, [this, context] {
        if (m_cancelled.remove(context.requestId)) return;
        QVector<ChargingOrder> orders;
        for (const ChargingOrder &order : m_orders)
            orders.append(order);
        emit orderHistoryReady(context, orders);
    });
}

void MockOrderService::queryOrderDetail(const RequestContext &context,
                                        const QString &orderId)
{
    if (!context.isValid() || context.isMutation() || orderId.isEmpty()) {
        fail(context, QStringLiteral("order-invalid-query"),
             QStringLiteral("订单详情查询参数无效。"));
        return;
    }
    QTimer::singleShot(0, this, [this, context, orderId] {
        if (m_cancelled.remove(context.requestId)) return;
        if (!m_orders.contains(orderId)) {
            fail(context, QStringLiteral("order-not-found"),
                 QStringLiteral("订单不存在。"));
            return;
        }
        emit orderDetailReady(context, m_orders.value(orderId));
    });
}

void MockOrderService::stopCharging(const RequestContext &context,
                                    const QString &orderId)
{
    if (!context.isValid() || !context.isMutation()) {
        fail(context, QStringLiteral("order-invalid-stop-request"),
             QStringLiteral("停止充电请求缺少 operationId。"));
        return;
    }
    if (!m_orders.contains(orderId) || m_orders.value(orderId).status != OrderStatus::Charging) {
        fail(context, QStringLiteral("order-not-charging"),
             QStringLiteral("订单当前不在充电中。"));
        return;
    }
    ChargingOrder order = m_orders.value(orderId);
    order.status = OrderStatus::PendingSettlement;
    order.endedAtUtc = QDateTime::currentDateTimeUtc();
    if (order.energyKwh <= 0.0) order.energyKwh = 9.7;
    if (order.amountCents <= 0)
        order.amountCents = qRound64(order.energyKwh * order.priceCentsPerKwhSnapshot);
    m_orders.insert(orderId, order);
    StopChargingResult result;
    result.requestId = context.requestId;
    result.operationId = context.operationId;
    result.order = order;
    m_stopResults.insert(context.operationId, result);
    QTimer::singleShot(0, this, [this, context, result] {
        emit chargingStopped(context, result);
    });
}

void MockOrderService::queryStopResult(const RequestContext &context,
                                       const QString &operationId)
{
    if (!context.isValid() || context.isMutation() || operationId.isEmpty()) {
        fail(context, QStringLiteral("order-invalid-stop-result-query"),
             QStringLiteral("停止结果查询参数无效。"));
        return;
    }
    StopOperationStatus status;
    status.requestId = context.requestId;
    status.operationId = operationId;
    if (m_stopResults.contains(operationId)) {
        status.state = StopOperationState::Succeeded;
        status.result = m_stopResults.value(operationId);
    } else {
        status.state = StopOperationState::Pending;
    }
    QTimer::singleShot(0, this, [this, context, status] {
        emit stopOperationStatusReady(context, status);
    });
}

void MockOrderService::cancel(const QString &requestId)
{
    if (!requestId.isEmpty()) m_cancelled.insert(requestId);
}

void MockOrderService::fail(const RequestContext &context, const QString &code,
                            const QString &message, bool retryable)
{
    ClientError error;
    error.requestId = context.requestId;
    error.operationId = context.operationId;
    error.code = code;
    error.displayMessage = message;
    error.retryable = retryable;
    emit requestFailed(error);
}
