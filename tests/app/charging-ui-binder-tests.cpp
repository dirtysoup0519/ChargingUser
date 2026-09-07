#include "app/charginguibinder.h"
#include "modules/charging/ichargingservice.h"

#include <QSignalSpy>
#include <QtTest>

class ControlledChargingService final : public IChargingService
{
public:
    using IChargingService::IChargingService;

    QVector<RequestContext> requests;
    QVector<QPair<QString, QString>> targets;
    QStringList cancellations;
    QVector<RequestContext> startRequests;
    QVector<RequestContext> resultQueries;
    QStringList queriedOperationIds;

    void loadConfirmation(const RequestContext &context,
                          const QString &stationId,
                          const QString &chargerId) override
    {
        requests.append(context);
        targets.append({stationId, chargerId});
    }

    void cancel(const QString &requestId) override
    {
        cancellations.append(requestId);
    }

    void startCharging(const RequestContext &context,
                       const QString &stationId,
                       const QString &chargerId) override
    {
        startRequests.append(context);
        targets.append({stationId, chargerId});
    }

    void queryStartResult(const RequestContext &context,
                          const QString &operationId) override
    {
        resultQueries.append(context);
        queriedOperationIds.append(operationId);
    }
};

namespace {
ChargeConfirmationSnapshot snapshot(const QString &stationId,
                                    const QString &chargerId)
{
    ChargeConfirmationSnapshot value;
    value.stationId = stationId;
    value.chargerId = chargerId;
    value.stationName = QStringLiteral("测试站");
    value.stationAddress = QStringLiteral("测试路 1 号");
    value.chargerCode = QStringLiteral("P-01");
    value.chargerType = QStringLiteral("快充");
    value.powerKw = 60.0;
    value.priceCentsPerKwh = 150;
    value.walletBalanceCents = 1234;
    value.canStart = true;
    value.canRecharge = true;
    return value;
}
}

class ChargingUiBinderTests final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<ChargeConfirmationViewState>();
    }

    void loadsAndFormatsReadOnlyConfirmation()
    {
        ControlledChargingService service;
        ChargingUiBinder binder(&service);
        QSignalSpy pages(&binder, &IChargingUiBinder::confirmationPageRequested);

        binder.chargeConfirmationRequested(QStringLiteral("station-a"),
                                           QStringLiteral("charger-a"));
        QCOMPARE(pages.count(), 1);
        QCOMPARE(service.requests.size(), 1);
        QVERIFY(service.requests.first().isValid());
        QVERIFY(!service.requests.first().isMutation());
        QCOMPARE(binder.currentState().status, ChargeConfirmationStatus::Loading);

        emit service.confirmationReady(
            service.requests.first(),
            snapshot(QStringLiteral("station-a"), QStringLiteral("charger-a")));
        const ChargeConfirmationViewState state = binder.currentState();
        QCOMPARE(state.status, ChargeConfirmationStatus::Ready);
        QCOMPARE(state.stationName, QStringLiteral("测试站"));
        QCOMPARE(state.powerText, QStringLiteral("60.0 kW"));
        QCOMPARE(state.energyPriceText, QStringLiteral("¥1.50/kWh"));
        QCOMPARE(state.walletBalanceText, QStringLiteral("¥12.34"));
        QVERIFY(!state.canStart);
        QCOMPARE(state.disabledReason, QStringLiteral("订单启动接口待接入。"));
    }

    void staleResponseCannotOverwriteNewTarget()
    {
        ControlledChargingService service;
        ChargingUiBinder binder(&service);
        binder.chargeConfirmationRequested(QStringLiteral("station-a"),
                                           QStringLiteral("charger-a"));
        const RequestContext first = service.requests.first();
        binder.chargeConfirmationRequested(QStringLiteral("station-b"),
                                           QStringLiteral("charger-b"));
        QCOMPARE(service.cancellations, QStringList{first.requestId});

        emit service.confirmationReady(
            first, snapshot(QStringLiteral("station-a"),
                            QStringLiteral("charger-a")));
        QCOMPARE(binder.currentState().stationId, QStringLiteral("station-b"));
        QCOMPARE(binder.currentState().status, ChargeConfirmationStatus::Loading);
    }

    void errorCanRetryAndBackCancelsPendingRequest()
    {
        ControlledChargingService service;
        ChargingUiBinder binder(&service);
        QSignalSpy back(&binder, &IChargingUiBinder::stationDetailPageRequested);
        binder.chargeConfirmationRequested(QStringLiteral("station-a"),
                                           QStringLiteral("charger-a"));
        const RequestContext first = service.requests.first();
        ClientError error;
        error.requestId = first.requestId;
        error.displayMessage = QStringLiteral("状态核对失败");
        error.retryable = true;
        emit service.requestFailed(error);
        QCOMPARE(binder.currentState().status, ChargeConfirmationStatus::Error);
        QVERIFY(binder.currentState().canRetry);

        binder.confirmationRefreshRequested();
        QCOMPARE(service.requests.size(), 2);
        const QString pendingId = service.requests.last().requestId;
        binder.backRequested();
        QVERIFY(service.cancellations.contains(pendingId));
        QCOMPARE(back.count(), 1);
    }

    void directStartCallIsRejectedWhileOrderInterfaceIsUnavailable()
    {
        ControlledChargingService service;
        ChargingUiBinder binder(&service);
        QSignalSpy states(&binder, &IChargingUiBinder::confirmationStateChanged);
        binder.chargeConfirmationRequested(QStringLiteral("station-a"),
                                           QStringLiteral("charger-a"));
        emit service.confirmationReady(
            service.requests.first(),
            snapshot(QStringLiteral("station-a"), QStringLiteral("charger-a")));
        const int before = states.count();
        binder.startChargingRequested(QStringLiteral("station-a"),
                                      QStringLiteral("charger-a"));
        QCOMPARE(states.count(), before);
        QVERIFY(!binder.currentState().canStart);
    }

    void resultUnknownQueriesOriginalOperationIdWithoutRestarting()
    {
        ControlledChargingService service;
        ChargingUiBinder binder(&service);
        binder.chargeConfirmationRequested(QStringLiteral("station-a"),
                                           QStringLiteral("charger-a"));
        ChargeConfirmationSnapshot ready =
            snapshot(QStringLiteral("station-a"), QStringLiteral("charger-a"));
        ready.startOperationSupported = true;
        emit service.confirmationReady(service.requests.first(), ready);
        QVERIFY(binder.currentState().canStart);

        binder.startChargingRequested(QStringLiteral("station-a"),
                                      QStringLiteral("charger-a"));
        QCOMPARE(service.startRequests.size(), 1);
        const RequestContext start = service.startRequests.first();
        QVERIFY(start.isMutation());
        QVERIFY(!start.operationId.isEmpty());
        QCOMPARE(binder.currentState().status,
                 ChargeConfirmationStatus::Submitting);

        ClientError unknown;
        unknown.requestId = start.requestId;
        unknown.operationId = start.operationId;
        unknown.resultUnknown = true;
        emit service.requestFailed(unknown);
        QCOMPARE(binder.currentState().status,
                 ChargeConfirmationStatus::ResultUnknown);
        QCOMPARE(service.startRequests.size(), 1);
        QCOMPARE(service.resultQueries.size(), 1);
        QCOMPARE(service.queriedOperationIds.first(), start.operationId);
        QVERIFY(!service.resultQueries.first().isMutation());

        StartChargingResult result;
        result.requestId = service.resultQueries.first().requestId;
        result.operationId = start.operationId;
        result.orderId = QStringLiteral("order-1");
        result.stationId = QStringLiteral("station-a");
        result.chargerId = QStringLiteral("charger-a");
        ChargingOperationStatus operation;
        operation.requestId = result.requestId;
        operation.operationId = start.operationId;
        operation.state = ChargingOperationState::Succeeded;
        operation.result = result;
        QSignalSpy session(&binder,
                           &IChargingUiBinder::chargingSessionRequested);
        emit service.startOperationStatusReady(service.resultQueries.first(),
                                               operation);
        QCOMPARE(session.count(), 1);
        QCOMPARE(session.first().at(0).value<StartChargingResult>().orderId,
                 QStringLiteral("order-1"));
        QCOMPARE(service.startRequests.size(), 1);
    }
};

QTEST_GUILESS_MAIN(ChargingUiBinderTests)

#include "charging-ui-binder-tests.moc"
