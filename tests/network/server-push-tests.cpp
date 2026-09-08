#include "backendclient.h"
#include "inetworktransport.h"
#include "massagehandler.h"
#include "protocol.h"
#include "serverpushdispatcher.h"

#include <QSignalSpy>
#include <QtTest>

class FakeTransport final : public INetworkTransport
{
public:
    explicit FakeTransport(QObject *parent = nullptr) : INetworkTransport(parent) {}
    void connectToServer() override {}
    void disconnectFromServer() override {}
    bool isConnected() const override { return true; }
    bool send(const QByteArray &) override { return true; }
};

class ServerPushTests final : public QObject
{
    Q_OBJECT
private slots:
    void filtersMismatchedIdentity();
    void dispatchesProgressAndBalance();
};

void ServerPushTests::filtersMismatchedIdentity()
{
    FakeTransport transport;
    BackendClient backend(&transport);
    backend.start();
    emit transport.connected();
    ServerPushDispatcher dispatcher(&backend);
    dispatcher.setIdentity(QStringLiteral("U1"));
    QSignalSpy progress(&dispatcher, &ServerPushDispatcher::chargingProgress);
    transport.dataReceived(MassageHandler::pack(
        CHG_PROGRESS, QJsonObject{{QStringLiteral("username"), QStringLiteral("U2")},
                                  {QStringLiteral("orderNo"), QStringLiteral("o-1")}}));
    QCOMPARE(progress.count(), 0);
}

void ServerPushTests::dispatchesProgressAndBalance()
{
    FakeTransport transport;
    BackendClient backend(&transport);
    backend.start();
    emit transport.connected();
    ServerPushDispatcher dispatcher(&backend);
    dispatcher.setIdentity(QStringLiteral("U1"));
    QSignalSpy progress(&dispatcher, &ServerPushDispatcher::chargingProgress);
    QSignalSpy balance(&dispatcher, &ServerPushDispatcher::balanceChanged);
    transport.dataReceived(MassageHandler::pack(
        CHG_PROGRESS, QJsonObject{{QStringLiteral("orderNo"), QStringLiteral("o-1")},
                                  {QStringLiteral("percent"), 40},
                                  {QStringLiteral("amountCents"), 123}}));
    transport.dataReceived(MassageHandler::pack(
        RECHARGE_ACK, QJsonObject{{QStringLiteral("balanceCents"), 500}}));
    QCOMPARE(progress.count(), 1);
    QCOMPARE(progress.first().at(0).value<ChargingProgressNotice>().percent, 40);
    QCOMPARE(balance.count(), 1);
}

QTEST_GUILESS_MAIN(ServerPushTests)
#include "server-push-tests.moc"
