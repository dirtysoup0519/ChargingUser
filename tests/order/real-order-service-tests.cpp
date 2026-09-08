#include "backendclient.h"
#include "inetworktransport.h"
#include "massagehandler.h"
#include "modules/order/iorderservice.h"
#include "realorderservice.h"

#include <QSignalSpy>
#include <QtTest>

#include <algorithm>

namespace {

RequestContext readOnlyContext(const QString &requestId)
{
    return {requestId, {}};
}

/**
 * 受控传输替身：记录出站帧，入站由测试注入；与 real-charger 测试同款回环。
 */
class FakeTransport final : public INetworkTransport
{
public:
    explicit FakeTransport(QObject *parent = nullptr)
        : INetworkTransport(parent)
    {
    }

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

QJsonObject payloadOfSentFrame(const QByteArray &frame)
{
    MassageHandler decoder;
    QJsonObject payload;
    QObject::connect(&decoder, &MassageHandler::frameReady,
                     [&payload](int, const QByteArray &data) {
        payload = MassageHandler::fromPayload(data);
    });
    decoder.feed(frame);
    return payload;
}

/** 过滤连接时自动发出的 107 存活心跳，业务帧序断言不受影响。 */
QVector<QPair<int, QJsonObject>>
businessFrames(const QVector<QByteArray> &bytesList)
{
    MassageHandler decoder;
    QVector<QPair<int, QJsonObject>> frames;
    QObject::connect(&decoder, &MassageHandler::frameReady,
                     [&frames](int msgType, const QByteArray &data) {
        frames.append(qMakePair(msgType, MassageHandler::fromPayload(data)));
    });
    for (const QByteArray &bytes : bytesList) {
        decoder.feed(bytes);
    }
    frames.erase(std::remove_if(frames.begin(), frames.end(),
                                [](const QPair<int, QJsonObject> &frame) {
                                    return frame.first == HEARTBEAT;
                                }),
                 frames.end());
    return frames;
}

QJsonObject dataPayload(const QJsonArray &orders)
{
    return QJsonObject{{QStringLiteral("orders"), orders}};
}

QJsonObject orderRecord(const QString &orderNo, const QString &status,
                        int amountCents, double kwh)
{
    return QJsonObject{
        {QStringLiteral("orderNo"), orderNo},
        {QStringLiteral("username"), QStringLiteral("U1")},
        {QStringLiteral("stationName"), QStringLiteral("A站")},
        {QStringLiteral("chargerCode"), QStringLiteral("CP-1")},
        {QStringLiteral("status"), status},
        {QStringLiteral("priceCents"), 150},
        {QStringLiteral("kwh"), kwh},
        {QStringLiteral("amountCents"), amountCents},
        {QStringLiteral("startedAt"),
         QStringLiteral("2026-09-08 10:00:00")},
    };
}

} // namespace

class RealOrderServiceTests : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void initTestCase();

    void queryActiveOrderSendsUsernameCondition();
    void activeOrderPrefersChargingOverPending();
    void noActiveOrderEmitsEmptyOptional();
    void orderDetailMatchesByOrderNo();
    void detailNotFoundFailsWithOrderNotFound();
    void serverErrorFailsWithBizCode();
    void malformedPayloadFailsWithBadResponse();
    void timeoutFailsWithRequestTimeout();
    void disconnectFailsPendingAsConnectionLost();
    void cancelDropsLateResponses();
    void identityMissingFailsFast();
    void concurrentQueryIsRejected();
    void readonlyAndInvalidInputsAreRejected();
    void yuanFallbackRoundsToCents();

private:
    void feedResponse(int msgType, const QJsonObject &payload);

    FakeTransport *m_transport = nullptr;
    BackendClient *m_backend = nullptr;
    RealOrderService *m_service = nullptr;
};

void RealOrderServiceTests::initTestCase()
{
    qRegisterMetaType<RequestContext>();
    qRegisterMetaType<std::optional<ChargingOrder>>();
    qRegisterMetaType<ChargingOrder>();
    qRegisterMetaType<ClientError>();
}

void RealOrderServiceTests::init()
{
    // 每个用例独立的 transport/backend/service，互不污染。
    m_transport = new FakeTransport(this);
    m_backend = new BackendClient(m_transport, this);
    m_service = new RealOrderService(m_backend, this);
    m_service->setIdentity(QStringLiteral("U1"));
    m_backend->start();
    // BackendClient 在构造时已连接 transport 信号；直接触发 connected。
    emit m_transport->connected();
}

void RealOrderServiceTests::feedResponse(int msgType, const QJsonObject &payload)
{
    // 入站帧走真实链路：transport → BackendClient → MassageHandler → service。
    emit m_transport->dataReceived(MassageHandler::pack(msgType, payload));
}

void RealOrderServiceTests::queryActiveOrderSendsUsernameCondition()
{
    QSignalSpy readySpy(m_service, &IOrderService::activeOrderReady);
    m_service->queryActiveOrder(readOnlyContext(QStringLiteral("req-1")));

    const auto frames = businessFrames(m_transport->sentFrames);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.first().first, ORDERQRY_REQ);
    QCOMPARE(frames.first().second.value(QLatin1String("username")).toString(),
             QStringLiteral("U1"));
    QCOMPARE(frames.first().second.value(QLatin1String("requestId")).toString(),
             QStringLiteral("req-1"));

    feedResponse(ORDERQRY_ACK, dataPayload({}));
    QCOMPARE(readySpy.count(), 1);
    const auto active = readySpy.first().at(1)
                            .value<std::optional<ChargingOrder>>();
    QVERIFY(!active.has_value());
}

void RealOrderServiceTests::activeOrderPrefersChargingOverPending()
{
    QSignalSpy readySpy(m_service, &IOrderService::activeOrderReady);
    m_service->queryActiveOrder(readOnlyContext(QStringLiteral("req-2")));
    feedResponse(ORDERQRY_ACK,
                 dataPayload({orderRecord(QStringLiteral("NO-2"),
                                          QStringLiteral("PendingSettlement"), 100, 1.0),
                              orderRecord(QStringLiteral("NO-1"),
                                          QStringLiteral("Charging"), 525, 3.5),
                              orderRecord(QStringLiteral("NO-3"),
                                          QStringLiteral("Settled"), 200, 2.0)}));

    QCOMPARE(readySpy.count(), 1);
    const auto active = readySpy.first().at(1)
                            .value<std::optional<ChargingOrder>>();
    QVERIFY(active.has_value());
    QCOMPARE(active->orderId, QStringLiteral("NO-1"));
    QCOMPARE(active->status, OrderStatus::Charging);
    QCOMPARE(active->amountCents, 525);
    QCOMPARE(active->priceCentsPerKwhSnapshot, 150);
    QCOMPARE(active->energyKwh, 3.5);
    QVERIFY(active->startedAtUtc.isValid());
    QVERIFY(!active->endedAtUtc.has_value());
}

void RealOrderServiceTests::noActiveOrderEmitsEmptyOptional()
{
    QSignalSpy readySpy(m_service, &IOrderService::activeOrderReady);
    m_service->queryActiveOrder(readOnlyContext(QStringLiteral("req-3")));
    feedResponse(ORDERQRY_ACK,
                 dataPayload({orderRecord(QStringLiteral("NO-9"),
                                          QStringLiteral("Settled"), 200, 2.0)}));
    QCOMPARE(readySpy.count(), 1);
    QVERIFY(!readySpy.first().at(1).value<std::optional<ChargingOrder>>().has_value());
}

void RealOrderServiceTests::orderDetailMatchesByOrderNo()
{
    QSignalSpy readySpy(m_service, &IOrderService::orderDetailReady);
    m_service->queryOrderDetail(readOnlyContext(QStringLiteral("req-4")),
                                QStringLiteral("NO-2"));

    feedResponse(ORDERQRY_ACK,
                 dataPayload({orderRecord(QStringLiteral("NO-1"),
                                          QStringLiteral("Charging"), 525, 3.5),
                              orderRecord(QStringLiteral("NO-2"),
                                          QStringLiteral("PendingSettlement"), 480, 3.2)}));
    QCOMPARE(readySpy.count(), 1);
    QCOMPARE(readySpy.first().at(1).value<ChargingOrder>().orderId,
             QStringLiteral("NO-2"));
}

void RealOrderServiceTests::detailNotFoundFailsWithOrderNotFound()
{
    QSignalSpy failedSpy(m_service, &IOrderService::requestFailed);
    m_service->queryOrderDetail(readOnlyContext(QStringLiteral("req-5")),
                                QStringLiteral("NO-404"));
    feedResponse(ORDERQRY_ACK,
                 dataPayload({orderRecord(QStringLiteral("NO-1"),
                                          QStringLiteral("Charging"), 525, 3.5)}));
    QCOMPARE(failedSpy.count(), 1);
    const ClientError error = failedSpy.first().at(0).value<ClientError>();
    QCOMPARE(error.code, QStringLiteral("order-not-found"));
    QVERIFY(!error.retryable);
}

void RealOrderServiceTests::serverErrorFailsWithBizCode()
{
    QSignalSpy failedSpy(m_service, &IOrderService::requestFailed);
    m_service->queryActiveOrder(readOnlyContext(QStringLiteral("req-6")));
    feedResponse(DB_ERROR,
                 QJsonObject{{QStringLiteral("code"), QStringLiteral("DB_ERROR")}});
    QCOMPARE(failedSpy.count(), 1);
    const ClientError error = failedSpy.first().at(0).value<ClientError>();
    QCOMPARE(error.code, QStringLiteral("DB_ERROR"));
    QVERIFY(error.retryable);
}

void RealOrderServiceTests::malformedPayloadFailsWithBadResponse()
{
    QSignalSpy failedSpy(m_service, &IOrderService::requestFailed);
    m_service->queryActiveOrder(readOnlyContext(QStringLiteral("req-7")));
    feedResponse(ORDERQRY_ACK,
                 QJsonObject{{QStringLiteral("orders"), QStringLiteral("oops")}});
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.first().at(0).value<ClientError>().code,
             QStringLiteral("bad-response"));
}

void RealOrderServiceTests::timeoutFailsWithRequestTimeout()
{
    QSignalSpy failedSpy(m_service, &IOrderService::requestFailed);
    m_service->setRequestTimeoutMs(30);
    m_service->queryActiveOrder(readOnlyContext(QStringLiteral("req-8")));
    QTest::qWait(150);
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.first().at(0).value<ClientError>().code,
             QStringLiteral("request-timeout"));
}

void RealOrderServiceTests::disconnectFailsPendingAsConnectionLost()
{
    QSignalSpy failedSpy(m_service, &IOrderService::requestFailed);
    m_service->queryActiveOrder(readOnlyContext(QStringLiteral("req-9")));
    emit m_transport->disconnected();
    QCOMPARE(failedSpy.count(), 1);
    const ClientError error = failedSpy.first().at(0).value<ClientError>();
    QCOMPARE(error.code, QStringLiteral("connection-lost"));
    QVERIFY(error.retryable);
}

void RealOrderServiceTests::cancelDropsLateResponses()
{
    QSignalSpy readySpy(m_service, &IOrderService::activeOrderReady);
    QSignalSpy failedSpy(m_service, &IOrderService::requestFailed);
    m_service->queryActiveOrder(readOnlyContext(QStringLiteral("req-10")));
    m_service->cancel(QStringLiteral("req-10"));
    feedResponse(ORDERQRY_ACK,
                 dataPayload({orderRecord(QStringLiteral("NO-1"),
                                          QStringLiteral("Charging"), 525, 3.5)}));
    QCOMPARE(readySpy.count(), 0);
    QCOMPARE(failedSpy.count(), 0);

    // 取消后在途关联已清空，相同 requestId 可再次发起。
    m_service->queryActiveOrder(readOnlyContext(QStringLiteral("req-10")));
    feedResponse(ORDERQRY_ACK, dataPayload({}));
    QCOMPARE(readySpy.count(), 1);
}

void RealOrderServiceTests::identityMissingFailsFast()
{
    QSignalSpy failedSpy(m_service, &IOrderService::requestFailed);
    m_service->setIdentity(QString());
    m_service->queryActiveOrder(readOnlyContext(QStringLiteral("req-11")));
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.first().at(0).value<ClientError>().code,
             QStringLiteral("order-identity-missing"));
    QCOMPARE(businessFrames(m_transport->sentFrames).size(), 0);
}

void RealOrderServiceTests::concurrentQueryIsRejected()
{
    QSignalSpy failedSpy(m_service, &IOrderService::requestFailed);
    m_service->queryActiveOrder(readOnlyContext(QStringLiteral("req-a")));
    m_service->queryActiveOrder(readOnlyContext(QStringLiteral("req-b")));
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.first().at(0).value<ClientError>().code,
             QStringLiteral("request-in-flight"));
}

void RealOrderServiceTests::readonlyAndInvalidInputsAreRejected()
{
    QSignalSpy failedSpy(m_service, &IOrderService::requestFailed);

    RequestContext mutation;
    mutation.requestId = QStringLiteral("req-m");
    mutation.operationId = QStringLiteral("op-1");
    m_service->queryActiveOrder(mutation);
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.first().at(0).value<ClientError>().code,
             QStringLiteral("order-readonly-operation"));

    m_service->queryOrderDetail(readOnlyContext(QStringLiteral("req-d")),
                                QStringLiteral("  "));
    QCOMPARE(failedSpy.count(), 2);

    QCOMPARE(businessFrames(m_transport->sentFrames).size(), 0);
}

void RealOrderServiceTests::yuanFallbackRoundsToCents()
{
    QSignalSpy readySpy(m_service, &IOrderService::activeOrderReady);
    m_service->queryActiveOrder(readOnlyContext(QStringLiteral("req-yuan")));
    QJsonObject record = orderRecord(QStringLiteral("NO-1"),
                                     QStringLiteral("Charging"), 0, 2.0);
    record.remove(QStringLiteral("amountCents"));
    record.insert(QStringLiteral("amount"), 5.25);
    feedResponse(ORDERQRY_ACK, dataPayload({record}));
    QCOMPARE(readySpy.count(), 1);
    const auto active = readySpy.first().at(1)
                            .value<std::optional<ChargingOrder>>();
    QCOMPARE(active->amountCents, 525); // 5.25 元 → 525 分
}

QTEST_GUILESS_MAIN(RealOrderServiceTests)

#include "real-order-service-tests.moc"
