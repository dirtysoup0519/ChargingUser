#include "backendclient.h"
#include "inetworktransport.h"
#include "massagehandler.h"
#include "protocol.h"
#include "realreservationservice.h"

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

class RealReservationServiceTests final : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void reserveSendsIdentityAndMapsAck();
    void timeoutIsResultUnknown();

private:
    FakeTransport *m_transport = nullptr;
    BackendClient *m_backend = nullptr;
    RealReservationService *m_service = nullptr;
};

void RealReservationServiceTests::init()
{
    m_transport = new FakeTransport(this);
    m_backend = new BackendClient(m_transport, this);
    m_backend->start();
    emit m_transport->connected();
    m_service = new RealReservationService(m_backend, this);
    m_service->setIdentity(QStringLiteral("U13800138000"));
}

void RealReservationServiceTests::reserveSendsIdentityAndMapsAck()
{
    QSignalSpy created(m_service, &IReservationService::reservationCreated);
    m_service->reserve({QStringLiteral("reserve-1"), QStringLiteral("op-1")},
                       QStringLiteral("站点一"), QStringLiteral("A-01"), 7200);
    QCOMPARE(m_transport->sentFrames.size(), 1);
    QCOMPARE(frameType(m_transport->sentFrames.first()), RESERVE_REQ);
    const QJsonObject payload = framePayload(m_transport->sentFrames.first());
    QCOMPARE(payload.value(QStringLiteral("username")).toString(),
             QStringLiteral("U13800138000"));
    QCOMPARE(payload.value(QStringLiteral("chargerCode")).toString(),
             QStringLiteral("A-01"));
    QCOMPARE(payload.value(QStringLiteral("durationSeconds")).toInt(), 7200);

    feed(m_transport, RESERVE_ACK,
         QJsonObject{{QStringLiteral("reserveId"), QStringLiteral("r-1")},
                     {QStringLiteral("chargerCode"), QStringLiteral("A-01")},
                     {QStringLiteral("balanceCents"), 8000},
                     {QStringLiteral("reserveAt"), QStringLiteral("2026-09-08T12:00:00Z")},
                     {QStringLiteral("expireAt"), QStringLiteral("2026-09-08T14:00:00Z")}});
    QCOMPARE(created.count(), 1);
    const ReservationResult result =
        created.first().at(1).value<ReservationResult>();
    QCOMPARE(result.reservationId, QStringLiteral("r-1"));
    QCOMPARE(result.stationId, QStringLiteral("站点一"));
    QCOMPARE(result.balanceCents, qint64(8000));
}

void RealReservationServiceTests::timeoutIsResultUnknown()
{
    m_service->setRequestTimeoutMs(20);
    QSignalSpy failed(m_service, &IReservationService::requestFailed);
    m_service->reserve({QStringLiteral("reserve-timeout"), QStringLiteral("op-timeout")},
                       QStringLiteral("站点一"), QStringLiteral("A-01"), 7200);
    QTest::qWait(80);
    QCOMPARE(failed.count(), 1);
    QVERIFY(failed.first().at(0).value<ClientError>().resultUnknown);
}

QTEST_GUILESS_MAIN(RealReservationServiceTests)
#include "real-reservation-service-tests.moc"
