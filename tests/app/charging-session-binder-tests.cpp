#include "app/chargingsessionuibinder.h"
#include "modules/order/iorderservice.h"

#include <QSignalSpy>
#include <QtTest>

class ControlledOrderService final : public IOrderService
{
public:
    using IOrderService::IOrderService;
    QVector<RequestContext> detailRequests;
    QVector<RequestContext> stopRequests;
    QVector<RequestContext> resultQueries;
    QVector<RequestContext> activeOrdersQueries;
    QStringList orderIds;
    QStringList operationIds;
    QStringList cancellations;

    void queryActiveOrder(const RequestContext &) override {}
    // 合同新增：多活动订单查询（会话页多会话选择），测试桩记录调用即可。
    void queryActiveOrders(const RequestContext &context) override
    {
        activeOrdersQueries.append(context);
    }
    void queryOrderHistory(const RequestContext &) override {}
    void queryOrderDetail(const RequestContext &context,
                          const QString &orderId) override
    {
        detailRequests.append(context);
        orderIds.append(orderId);
    }
    void stopCharging(const RequestContext &context,
                      const QString &orderId) override
    {
        stopRequests.append(context);
        orderIds.append(orderId);
    }
    void queryStopResult(const RequestContext &context,
                         const QString &operationId) override
    {
        resultQueries.append(context);
        operationIds.append(operationId);
    }
    void cancel(const QString &requestId) override
    {
        cancellations.append(requestId);
    }
};

namespace {
ChargingOrder chargingOrder()
{
    ChargingOrder order;
    order.orderId = QStringLiteral("order-1");
    order.stationName = QStringLiteral("测试站");
    order.chargerCode = QStringLiteral("P-01");
    order.status = OrderStatus::Charging;
    order.energyKwh = 3.25;
    order.amountCents = 488;
    order.startedAtUtc = QDateTime::currentDateTimeUtc();
    return order;
}
}

class ChargingSessionBinderTests final : public QObject
{
    Q_OBJECT
private slots:
    void loadsOrderAndExposesBusinessActions()
    {
        ControlledOrderService service;
        ChargingSessionUiBinder binder(&service);
        binder.sessionRequested(QStringLiteral("order-1"));
        QCOMPARE(service.detailRequests.size(), 1);
        QVERIFY(!service.detailRequests.first().isMutation());
        emit service.orderDetailReady(service.detailRequests.first(), chargingOrder());
        const auto state = binder.currentState();
        QCOMPARE(state.status, ChargingSessionStatus::Charging);
        QCOMPARE(state.energyText, QStringLiteral("3.25 kWh"));
        QCOMPARE(state.amountText, QStringLiteral("¥4.88"));
        QVERIFY(state.canStop);
        QVERIFY(state.canRefresh);
    }

    void stopUsesOneOperationIdAndRecoversUnknownResult()
    {
        ControlledOrderService service;
        ChargingSessionUiBinder binder(&service);
        binder.sessionRequested(QStringLiteral("order-1"));
        emit service.orderDetailReady(service.detailRequests.first(), chargingOrder());
        binder.stopChargingRequested();
        QCOMPARE(service.stopRequests.size(), 1);
        const RequestContext stop = service.stopRequests.first();
        QVERIFY(stop.isMutation());

        ClientError unknown;
        unknown.requestId = stop.requestId;
        unknown.operationId = stop.operationId;
        unknown.resultUnknown = true;
        emit service.requestFailed(unknown);
        QCOMPARE(service.resultQueries.size(), 1);
        QVERIFY(!service.resultQueries.first().isMutation());
        QCOMPARE(service.operationIds.first(), stop.operationId);
        QCOMPARE(service.stopRequests.size(), 1);

        StopChargingResult result;
        result.operationId = stop.operationId;
        result.order = chargingOrder();
        result.order.status = OrderStatus::PendingSettlement;
        StopOperationStatus status;
        status.operationId = stop.operationId;
        status.state = StopOperationState::Succeeded;
        status.result = result;
        emit service.stopOperationStatusReady(service.resultQueries.first(), status);
        QCOMPARE(binder.currentState().status, ChargingSessionStatus::Ended);
        QVERIFY(!binder.currentState().canStop);
        QCOMPARE(service.stopRequests.size(), 1);
    }

    void staleOrderResponseIsIgnored()
    {
        ControlledOrderService service;
        ChargingSessionUiBinder binder(&service);
        binder.sessionRequested(QStringLiteral("order-1"));
        const RequestContext old = service.detailRequests.first();
        binder.sessionRequested(QStringLiteral("order-2"));
        QVERIFY(service.cancellations.contains(old.requestId));
        emit service.orderDetailReady(old, chargingOrder());
        QCOMPARE(binder.currentState().orderId, QStringLiteral("order-2"));
        QCOMPARE(binder.currentState().status, ChargingSessionStatus::Loading);
    }

    void unknownOrderStateDoesNotExposeStopAction()
    {
        ControlledOrderService service;
        ChargingSessionUiBinder binder(&service);
        binder.sessionRequested(QStringLiteral("order-1"));
        ChargingOrder order = chargingOrder();
        order.status = OrderStatus::Unknown;
        emit service.orderDetailReady(service.detailRequests.first(), order);
        QCOMPARE(binder.currentState().status, ChargingSessionStatus::Error);
        QVERIFY(!binder.currentState().canStop);
        QVERIFY(binder.currentState().canRefresh);
    }

    void loadsAndSwitchesMultipleActiveOrders()
    {
        ControlledOrderService service;
        ChargingSessionUiBinder binder(&service);
        binder.activeSessionsRequested();
        QCOMPARE(service.activeOrdersQueries.size(), 1);
        ChargingOrder first = chargingOrder();
        ChargingOrder second = first;
        second.orderId = QStringLiteral("order-2");
        second.chargerCode = QStringLiteral("P-02");
        emit service.activeOrdersReady(service.activeOrdersQueries.first(), {first, second});
        QCOMPARE(binder.currentSessionsState().sessions.size(), 2);
        binder.activeSessionSelected(second.orderId);
        QCOMPARE(service.orderIds.last(), second.orderId);
    }
};

QTEST_GUILESS_MAIN(ChargingSessionBinderTests)
#include "charging-session-binder-tests.moc"
