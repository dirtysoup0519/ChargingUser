#include "modules/wallet/iwalletnetworkapi.h"
#include "modules/wallet/mockwalletservice.h"
#include "modules/wallet/walletservice.h"

#include <QSignalSpy>
#include <QtTest>

class ControlledWalletNetwork final : public IWalletNetworkApi
{
public:
    using IWalletNetworkApi::IWalletNetworkApi;
    WalletBackendCapabilities advertised;
    int queryCalls = 0;
    int rechargeCalls = 0;
    int paymentCalls = 0;
    int resultQueryCalls = 0;

    WalletBackendCapabilities capabilities() const override { return advertised; }
    void queryWallet(const RequestContext &) override { ++queryCalls; }
    void recharge(const RequestContext &, qint64) override { ++rechargeCalls; }
    void payOrder(const RequestContext &, const QString &) override { ++paymentCalls; }
    void queryOperationResult(const RequestContext &, const QString &) override
    { ++resultQueryCalls; }
    void cancel(const QString &) override {}
};

class WalletModuleTests final : public QObject
{
    Q_OBJECT
private slots:
    void protocolV25DoesNotEnableUnsafeMoneyOperations()
    {
        ControlledWalletNetwork network;
        network.advertised = WalletBackendCapabilities::protocolV25();
        WalletService service(&network);
        QSignalSpy failed(&service, &IWalletService::requestFailed);
        service.queryWallet({QStringLiteral("wallet-1"), {}});
        service.recharge({QStringLiteral("recharge-1"), QStringLiteral("op-1")}, 1000);
        service.payOrder({QStringLiteral("pay-1"), QStringLiteral("op-2")},
                         QStringLiteral("order-1"));
        QCOMPARE(failed.count(), 3);
        QCOMPARE(network.queryCalls, 0);
        QCOMPARE(network.rechargeCalls, 0);
        QCOMPARE(network.paymentCalls, 0);
    }

    void completeCapabilitiesForwardTypedRequests()
    {
        ControlledWalletNetwork network;
        network.advertised.walletSnapshotQuery = true;
        network.advertised.rechargeMessage = true;
        network.advertised.payOrderMessage = true;
        network.advertised.responseCorrelation = true;
        network.advertised.idempotentMoneyMutations = true;
        network.advertised.operationResultQuery = true;
        WalletService service(&network);
        service.queryWallet({QStringLiteral("wallet-1"), {}});
        service.recharge({QStringLiteral("recharge-1"), QStringLiteral("op-1")}, 1000);
        service.payOrder({QStringLiteral("pay-1"), QStringLiteral("op-2")},
                         QStringLiteral("order-1"));
        service.queryOperationResult({QStringLiteral("result-1"), {}},
                                     QStringLiteral("op-2"));
        QCOMPARE(network.queryCalls, 1);
        QCOMPARE(network.rechargeCalls, 1);
        QCOMPARE(network.paymentCalls, 1);
        QCOMPARE(network.resultQueryCalls, 1);
    }

    void mockSupportsReadOnlyDataAndNeverFakesMoneyMutation()
    {
        MockWalletService service;
        WalletSnapshot snapshot;
        snapshot.accountId = QStringLiteral("user-1");
        snapshot.balanceCents = 1234;
        service.setSnapshot(snapshot);
        QSignalSpy ready(&service, &IWalletService::walletReady);
        QSignalSpy succeeded(&service, &IWalletService::moneyOperationSucceeded);
        QSignalSpy failed(&service, &IWalletService::requestFailed);
        service.queryWallet({QStringLiteral("wallet-1"), {}});
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(ready.first().at(1).value<WalletSnapshot>().balanceCents, 1234);
        service.recharge({QStringLiteral("recharge-1"), QStringLiteral("op-1")}, 1000);
        QCOMPARE(failed.count(), 1);
        QCOMPARE(succeeded.count(), 0);
    }
};

QTEST_GUILESS_MAIN(WalletModuleTests)
#include "wallet-module-tests.moc"
