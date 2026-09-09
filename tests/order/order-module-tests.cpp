#include "modules/order/mockorderservice.h"
#include <QSignalSpy>
#include <QtTest>

class OrderModuleTests final : public QObject
{
    Q_OBJECT
private slots:
    void queryActiveOrderUsesReadOnlyContext()
    {
        MockOrderService service;
        ChargingOrder order;
        order.orderId = QStringLiteral("OD-1");
        order.status = OrderStatus::Charging;
        service.setOrders({order});
        QSignalSpy ready(&service, &IOrderService::activeOrderReady);
        service.queryActiveOrder({QStringLiteral("read-1"), {}});
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(ready.first().at(1).value<std::optional<ChargingOrder>>()
                     ->orderId, QStringLiteral("OD-1"));
    }

    void stopRequiresOperationIdAndUpdatesMockOrder()
    {
        MockOrderService service;
        QSignalSpy failed(&service, &IOrderService::requestFailed);
        QSignalSpy stopped(&service, &IOrderService::chargingStopped);
        service.stopCharging({QStringLiteral("stop-1"), {}},
                             QStringLiteral("OD-1"));
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.last().at(0).value<ClientError>().code,
                 QStringLiteral("order-invalid-stop-request"));
        ChargingOrder order;
        order.orderId = QStringLiteral("OD-1");
        order.status = OrderStatus::Charging;
        order.priceCentsPerKwhSnapshot = 150;
        service.setOrders({order});
        service.stopCharging({QStringLiteral("stop-2"), QStringLiteral("op-2")},
                             QStringLiteral("OD-1"));
        QTRY_COMPARE(stopped.count(), 1);
    }

    void stopResultQueryIsReadOnlyAndRequiresOriginalOperationId()
    {
        MockOrderService service;
        QSignalSpy failed(&service, &IOrderService::requestFailed);
        service.queryStopResult({QStringLiteral("query-1"), {}}, {});
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.last().at(0).value<ClientError>().code,
                 QStringLiteral("order-invalid-stop-result-query"));
        service.queryStopResult({QStringLiteral("query-2"), {}},
                                QStringLiteral("stop-op-1"));
        QCOMPARE(failed.count(), 1);
    }
};
QTEST_GUILESS_MAIN(OrderModuleTests)
#include "order-module-tests.moc"
