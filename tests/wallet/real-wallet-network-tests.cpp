#include "backendclient.h"
#include "inetworktransport.h"
#include "massagehandler.h"
#include "modules/wallet/iwalletnetworkapi.h"
#include "realwalletnetworkapi.h"

#include <QSignalSpy>
#include <QtTest>

namespace {

class FakeTransport final : public INetworkTransport
{
public:
    explicit FakeTransport(QObject *parent = nullptr) : INetworkTransport(parent) {}
    QVector<QByteArray> sentFrames;
    void connectToServer() override {}
    void disconnectFromServer() override {}
    bool isConnected() const override { return true; }
    bool send(const QByteArray &data) override
    {
        sentFrames.append(data);
        return true;
    }
};

int frameType(const QByteArray &frame)
{
    MassageHandler handler;
    int type = -1;
    QObject::connect(&handler, &MassageHandler::frameReady,
                     [&type](int value, const QByteArray &) { type = value; });
    handler.feed(frame);
    return type;
}

QJsonObject framePayload(const QByteArray &frame)
{
    MassageHandler handler;
    QJsonObject payload;
    QObject::connect(&handler, &MassageHandler::frameReady,
                     [&payload](int, const QByteArray &data) {
        payload = QJsonDocument::fromJson(data).object();
    });
    handler.feed(frame);
    return payload;
}

void feed(FakeTransport *transport, int type, const QJsonObject &payload)
{
    transport->dataReceived(MassageHandler::pack(type, payload));
}

} // namespace

class RealWalletNetworkTests final : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void queryWalletReadsUserThenTransactions();
    void rechargeSendsCentsAndHandlesAck();
    void timeoutMarksRechargeResultUnknown();
    void malformedWalletResponseFails();

private:
    FakeTransport *m_transport = nullptr;
    BackendClient *m_backend = nullptr;
    RealWalletNetworkApi *m_api = nullptr;
};

void RealWalletNetworkTests::init()
{
    m_transport = new FakeTransport(this);
    m_backend = new BackendClient(m_transport, this);
    m_backend->start();
    emit m_transport->connected();
    m_api = new RealWalletNetworkApi(m_backend, this);
    m_api->setIdentity(QStringLiteral("U13800138000"));
    m_transport->sentFrames.clear();
}

void RealWalletNetworkTests::queryWalletReadsUserThenTransactions()
{
    QSignalSpy ready(m_api, &IWalletNetworkApi::walletReady);
    m_api->queryWallet({QStringLiteral("wallet-1"), {}});
    QCOMPARE(m_transport->sentFrames.size(), 1);
    QCOMPARE(frameType(m_transport->sentFrames.first()), GETDATA);
    QCOMPARE(framePayload(m_transport->sentFrames.first())
                 .value(QStringLiteral("table")).toString(), QStringLiteral("user"));

    feed(m_transport, DATA,
         QJsonObject{{QStringLiteral("data"), QJsonArray{
             QJsonObject{{QStringLiteral("username"), QStringLiteral("U13800138000")},
                         {QStringLiteral("balanceCents"), 1234}}}}});
    QCOMPARE(m_transport->sentFrames.size(), 2);
    QCOMPARE(framePayload(m_transport->sentFrames.last())
                 .value(QStringLiteral("table")).toString(),
             QStringLiteral("walletTransaction"));

    feed(m_transport, DATA,
         QJsonObject{{QStringLiteral("data"), QJsonArray{
             QJsonObject{{QStringLiteral("id"), QStringLiteral("tx-1")},
                         {QStringLiteral("type"), QStringLiteral("RECHARGE")},
                         {QStringLiteral("amountCents"), 500},
                         {QStringLiteral("balanceAfterCents"), 1234}}}}});
    QCOMPARE(ready.count(), 1);
    const WalletSnapshot snapshot = ready.first().at(1).value<WalletSnapshot>();
    QCOMPARE(snapshot.accountId, QStringLiteral("U13800138000"));
    QCOMPARE(snapshot.balanceCents, qint64(1234));
    QCOMPARE(snapshot.recentTransactions.size(), 1);
}

void RealWalletNetworkTests::rechargeSendsCentsAndHandlesAck()
{
    QSignalSpy succeeded(m_api, &IWalletNetworkApi::moneyOperationSucceeded);
    m_api->recharge({QStringLiteral("req-r"), QStringLiteral("op-r")}, 5000);
    QCOMPARE(m_transport->sentFrames.size(), 1);
    QCOMPARE(frameType(m_transport->sentFrames.first()), RECHARGE_REQ);
    const QJsonObject payload = framePayload(m_transport->sentFrames.first());
    QCOMPARE(payload.value(QStringLiteral("username")).toString(),
             QStringLiteral("U13800138000"));
    QCOMPARE(payload.value(QStringLiteral("amountCents")).toInt(), 5000);

    feed(m_transport, RECHARGE_ACK,
         QJsonObject{{QStringLiteral("ok"), true},
                     {QStringLiteral("balanceCents"), 6234},
                     {QStringLiteral("transactionId"), QStringLiteral("tx-r")}});
    QCOMPARE(succeeded.count(), 1);
    const MoneyOperationResult result =
        succeeded.first().at(1).value<MoneyOperationResult>();
    QCOMPARE(result.balanceCents, qint64(6234));
    QCOMPARE(result.operationId, QStringLiteral("op-r"));
}

void RealWalletNetworkTests::timeoutMarksRechargeResultUnknown()
{
    m_api->setRequestTimeoutMs(20);
    QSignalSpy failed(m_api, &IWalletNetworkApi::requestFailed);
    m_api->recharge({QStringLiteral("req-timeout"), QStringLiteral("op-timeout")}, 100);
    QTest::qWait(80);
    QCOMPARE(failed.count(), 1);
    const ClientError error = failed.first().at(0).value<ClientError>();
    QVERIFY(error.resultUnknown);
    QCOMPARE(error.operationId, QStringLiteral("op-timeout"));
}

void RealWalletNetworkTests::malformedWalletResponseFails()
{
    QSignalSpy failed(m_api, &IWalletNetworkApi::requestFailed);
    m_api->queryWallet({QStringLiteral("req-bad"), {}});
    feed(m_transport, DATA,
         QJsonObject{{QStringLiteral("data"), QStringLiteral("bad")} });
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.first().at(0).value<ClientError>().code,
             QStringLiteral("bad-response"));
}

QTEST_GUILESS_MAIN(RealWalletNetworkTests)
#include "real-wallet-network-tests.moc"
