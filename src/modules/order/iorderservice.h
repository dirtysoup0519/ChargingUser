#pragma once

#include "common/clienterror.h"
#include "common/requestcontext.h"
#include "modules/order/ordertypes.h"

#include <QObject>

class IOrderService : public QObject
{
    Q_OBJECT
public:
    explicit IOrderService(QObject *parent = nullptr) : QObject(parent) {}
    ~IOrderService() override = default;
public slots:
    virtual void queryActiveOrder(const RequestContext &context) = 0;
    /** Returns every active order for accounts that may charge concurrently. */
    virtual void queryActiveOrders(const RequestContext &context) = 0;
    virtual void queryOrderDetail(const RequestContext &context,
                                  const QString &orderId) = 0;
    virtual void stopCharging(const RequestContext &context,
                              const QString &orderId) = 0;
    virtual void queryStopResult(const RequestContext &context,
                                 const QString &operationId) = 0;
    virtual void cancel(const QString &requestId) = 0;
signals:
    void activeOrderReady(const RequestContext &context,
                          const std::optional<ChargingOrder> &order);
    void activeOrdersReady(const RequestContext &context,
                           const QVector<ChargingOrder> &orders);
    void orderDetailReady(const RequestContext &context,
                          const ChargingOrder &order);
    void chargingStopped(const RequestContext &context,
                         const StopChargingResult &result);
    void stopOperationStatusReady(const RequestContext &context,
                                  const StopOperationStatus &status);
    void requestFailed(const ClientError &error);
};
