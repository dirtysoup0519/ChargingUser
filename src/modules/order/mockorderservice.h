#pragma once

#include "modules/order/iorderservice.h"

#include <QHash>
#include <QSet>

class MockOrderService final : public IOrderService
{
    Q_OBJECT
public:
    explicit MockOrderService(QObject *parent = nullptr);
    void setOrders(const QVector<ChargingOrder> &orders);
public slots:
    void queryActiveOrder(const RequestContext &context) override;
    void queryOrderDetail(const RequestContext &context,
                          const QString &orderId) override;
    void stopCharging(const RequestContext &context,
                      const QString &orderId) override;
    void queryStopResult(const RequestContext &context,
                         const QString &operationId) override;
    void cancel(const QString &requestId) override;
private:
    void fail(const RequestContext &context, const QString &code,
              const QString &message, bool retryable = false);
    QHash<QString, ChargingOrder> m_orders;
    QSet<QString> m_cancelled;
};
