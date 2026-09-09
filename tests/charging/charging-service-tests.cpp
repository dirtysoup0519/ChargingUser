#include "modules/charging/chargingservice.h"
#include "modules/charging/ichargingnetworkapi.h"

#include <QSignalSpy>
#include <QtTest>

class FakeChargingNetwork final : public IChargingNetworkApi
{
public:
    ChargingBackendCapabilities value;
    int confirmationCalls = 0;
    int startCalls = 0;
    int resultCalls = 0;
    ChargingBackendCapabilities capabilities() const override { return value; }
    void loadConfirmation(const RequestContext &, const QString &,
                          const QString &) override { ++confirmationCalls; }
    void startCharging(const RequestContext &, const QString &,
                       const QString &) override { ++startCalls; }
    void queryStartResult(const RequestContext &,
                          const QString &) override { ++resultCalls; }
    void cancel(const QString &) override {}
};

class ChargingServiceTests final : public QObject
{
    Q_OBJECT
private slots:
    void protocolV25KeepsRealMutationsDisabled()
    {
        FakeChargingNetwork network;
        network.value = ChargingBackendCapabilities::protocolV25();
        ChargingService service(&network);
        QSignalSpy failed(&service, &IChargingService::requestFailed);
        service.loadConfirmation({QStringLiteral("read-1"), {}},
                                 QStringLiteral("station-a"),
                                 QStringLiteral("charger-a"));
        QCOMPARE(network.confirmationCalls, 0);
        QCOMPARE(failed.last().at(0).value<ClientError>().code,
                 QStringLiteral("charging-stable-id-contract-required"));

        service.startCharging({QStringLiteral("start-1"), QStringLiteral("op-1")},
                              QStringLiteral("station-a"),
                              QStringLiteral("charger-a"));
        QCOMPARE(network.startCalls, 0);
        QCOMPARE(failed.last().at(0).value<ClientError>().code,
                 QStringLiteral("charging-idempotency-contract-required"));
    }

    void completeCapabilitiesAllowForwarding()
    {
        FakeChargingNetwork network;
        network.value = {true, true, true, true, true};
        ChargingService service(&network);
        service.loadConfirmation({QStringLiteral("read-1"), {}},
                                 QStringLiteral("station-a"),
                                 QStringLiteral("charger-a"));
        service.startCharging({QStringLiteral("start-1"), QStringLiteral("op-1")},
                              QStringLiteral("station-a"),
                              QStringLiteral("charger-a"));
        service.queryStartResult({QStringLiteral("query-1"), {}},
                                 QStringLiteral("op-1"));
        QCOMPARE(network.confirmationCalls, 1);
        QCOMPARE(network.startCalls, 1);
        QCOMPARE(network.resultCalls, 1);
    }
};

QTEST_GUILESS_MAIN(ChargingServiceTests)
#include "charging-service-tests.moc"
