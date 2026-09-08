#include "modules/order/mockorderservice.h"

#include <QTimer>

MockOrderService::MockOrderService(QObject *parent) : IOrderService(parent) {}

void MockOrderService::setOrders(const QVector<ChargingOrder> &orders)
{
    m_orders.clear();
    for (const ChargingOrder &order : orders)
        if (!order.orderId.isEmpty()) m_orders.insert(order.orderId, order);
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
    Q_UNUSED(orderId)
    if (!context.isValid() || !context.isMutation()) {
        fail(context, QStringLiteral("order-invalid-stop-request"),
             QStringLiteral("停止充电请求缺少 operationId。"));
        return;
    }
    fail(context, QStringLiteral("order-stop-not-configured"),
         QStringLiteral("停止充电接口尚未接入。"));
}

void MockOrderService::queryStopResult(const RequestContext &context,
                                       const QString &operationId)
{
    if (!context.isValid() || context.isMutation() || operationId.isEmpty()) {
        fail(context, QStringLiteral("order-invalid-stop-result-query"),
             QStringLiteral("停止结果查询参数无效。"));
        return;
    }
    fail(context, QStringLiteral("order-stop-result-not-configured"),
         QStringLiteral("停止结果查询接口尚未接入。"));
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
