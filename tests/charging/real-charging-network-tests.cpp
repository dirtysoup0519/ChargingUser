#include "backendclient.h"
#include "inetworktransport.h"
#include "massagehandler.h"
#include "protocol.h"
#include "realchargingnetworkapi.h"

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

class RealChargingNetworkTests final : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void confirmationReadsStationAndUserBalance();
    void unsafeStartCapabilityRemainsDisabled();

private:
    FakeTransport *m_transport = nullptr;
    BackendClient *m_backend = nullptr;
    RealChargingNetworkApi *m_api = nullptr;
};

void RealChargingNetworkTests::init()
{
    m_transport = new FakeTransport(this);
    m_backend = new BackendClient(m_transport, this);
    m_backend->start();
    emit m_transport->connected();
    m_api = new RealChargingNetworkApi(m_backend, this);
    m_api->setIdentity(QStringLiteral("U13800138000"));
}

void RealChargingNetworkTests::confirmationReadsStationAndUserBalance()
{
    QSignalSpy ready(m_api, &IChargingNetworkApi::confirmationReady);
    m_api->loadConfirmation({QStringLiteral("confirm-1"), {}},
                            QStringLiteral("站点一"),
                            QStringLiteral("A-01"));
    QCOMPARE(m_transport->sentFrames.size(), 1);
    QCOMPARE(frameType(m_transport->sentFrames.first()), STATION_QRY_REQ);
    QCOMPARE(framePayload(m_transport->sentFrames.first())
                 .value(QStringLiteral("stationName")).toString(),
             QStringLiteral("站点一"));

    const QJsonObject charger{{QStringLiteral("chargerCode"), QStringLiteral("A-01")},
                              {QStringLiteral("type"), QStringLiteral("快充")},
                              {QStringLiteral("powerKw"), 60},
                              {QStringLiteral("online"), true},
                              {QStringLiteral("businessStatus"), CHARGER_IDLE}};
    const QJsonObject station{{QStringLiteral("stationName"), QStringLiteral("站点一")},
                              {QStringLiteral("address"), QStringLiteral("地址")},
                              {QStringLiteral("priceCents"), 80},
                              {QStringLiteral("chargers"), QJsonArray{charger}}};
    feed(m_transport, STATION_QRY_ACK,
         QJsonObject{{QStringLiteral("stations"), QJsonArray{station}}});
    QCOMPARE(m_transport->sentFrames.size(), 2);
    QCOMPARE(frameType(m_transport->sentFrames.last()), GETDATA);

    feed(m_transport, DATA,
         QJsonObject{{QStringLiteral("data"), QJsonArray{
             QJsonObject{{QStringLiteral("username"), QStringLiteral("U13800138000")},
                         {QStringLiteral("balanceCents"), 1234}}}}});
    QCOMPARE(ready.count(), 1);
    const ChargeConfirmationSnapshot snapshot =
        ready.first().at(1).value<ChargeConfirmationSnapshot>();
    QCOMPARE(snapshot.stationId, QStringLiteral("站点一"));
    QCOMPARE(snapshot.chargerCode, QStringLiteral("A-01"));
    QCOMPARE(snapshot.walletBalanceCents.value(), qint64(1234));
    QVERIFY(!snapshot.startOperationSupported);
}

void RealChargingNetworkTests::unsafeStartCapabilityRemainsDisabled()
{
    QVERIFY(!m_api->capabilities().canStartChargingSafely());
}

QTEST_GUILESS_MAIN(RealChargingNetworkTests)
#include "real-charging-network-tests.moc"
