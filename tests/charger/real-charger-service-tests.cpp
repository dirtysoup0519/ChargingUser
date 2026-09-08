#include "backendclient.h"
#include "inetworktransport.h"
#include "massagehandler.h"
#include "modules/charger/ichargerservice.h"
#include "realchargerservice.h"

#include <QSignalSpy>
#include <QtTest>

namespace {

RequestContext readOnlyContext(const QString &requestId)
{
    return {requestId, {}};
}

/**
 * 受控传输替身：记录所有出站帧，入站数据由测试注入。
 * 出站帧是完整编码头+载荷，测试用独立 MassageHandler 解码还原类型与 JSON。
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
    MassageHandler handler;
    QJsonObject payload;
    QObject::connect(&handler, &MassageHandler::frameReady,
                     [&payload](int, const QByteArray &data) {
                         payload = QJsonDocument::fromJson(data).object();
                     });
    handler.feed(frame);
    return payload;
}

QJsonObject stationRecord(const QString &name, double lng, double lat, int priceCents)
{
    return QJsonObject{{QStringLiteral("stationName"), name},
                       {QStringLiteral("address"), name + QStringLiteral("路1号")},
                       // 管理员端/服务端 station 表使用的正式字段名。
                       {QStringLiteral("longitude"), lng},
                       {QStringLiteral("latitude"), lat},
                       {QStringLiteral("priceCents"), priceCents}};
}

QJsonObject chargerRecord(const QString &code,
                          const QString &stationName,
                          int businessStatus,
                          bool online)
{
    return QJsonObject{{QStringLiteral("chargerCode"), code},
                       {QStringLiteral("stationName"), stationName},
                       {QStringLiteral("businessStatus"), businessStatus},
                       {QStringLiteral("online"), online},
                       {QStringLiteral("type"), QStringLiteral("快充")},
                       {QStringLiteral("powerKw"), 60.0}};
}

void feedResponse(FakeTransport *transport, int msgType, const QJsonObject &payload)
{
    transport->dataReceived(MassageHandler::pack(msgType, payload));
}

QJsonObject dataPayload(const QJsonArray &records)
{
    return QJsonObject{{QStringLiteral("data"), records}};
}

} // namespace

/**
 * RealChargerService 协议适配器测试：
 * 全部经由真实 BackendClient + MassageHandler 编解码链路，只在传输层打桩。
 * 覆盖：两步取表、字段映射、坏行跳过、关联/超时/取消/断连、错误映射与并发约束。
 */
class RealChargerServiceTests final : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void queryStationsSendsStationThenChargerGetdata();
    void listAggregatesCountsFiltersAndPaginates();
    void detailReturnsChargersOfTargetStationOnly();
    void detailFailsEarlyWhenStationMissing();
    void serverErrorFailsPendingWithBizCode();
    void malformedDataFailsWithBadResponse();
    void timeoutFailsWithRequestTimeout();
    void disconnectFailsPendingAsConnectionLost();
    void cancelDropsLateResponses();
    void concurrentQueryIsRejectedUntilFirstSettles();
    void readonlyAndInvalidInputsAreRejectedLocally();
    void priceInYuanIsRoundedToCents();

private:
    FakeTransport *m_transport = nullptr;
    BackendClient *m_backend = nullptr;
    RealChargerService *m_service = nullptr;
};

void RealChargerServiceTests::init()
{
    m_transport = new FakeTransport(this);
    m_backend = new BackendClient(m_transport, this);
    m_backend->start();
    emit m_transport->connected();
    QCOMPARE(m_backend->connectionState(), ConnectionState::Connected);

    m_service = new RealChargerService(m_backend, this);
    // BackendClient 连接建立时会发送协议握手/心跳帧；本组测试只统计充电桩服务发出的请求。
    m_transport->sentFrames.clear();
}

void RealChargerServiceTests::queryStationsSendsStationThenChargerGetdata()
{
    QSignalSpy readySpy(m_service, &IChargerService::stationsReady);
    m_service->queryStations(readOnlyContext(QStringLiteral("req-1")), StationQuery{});

    // 第一步：station 表
    QCOMPARE(m_transport->sentFrames.size(), 1);
    QCOMPARE(payloadOfSentFrame(m_transport->sentFrames.at(0))
                 .value(QStringLiteral("table")).toString(),
             QStringLiteral("station"));

    // station 应答到达后才发 charger 请求（两步推进，规避 200 无表名歧义）
    feedResponse(m_transport, 200, dataPayload({stationRecord(QStringLiteral("A站"), 113.9, 22.5, 150)}));
    QCOMPARE(m_transport->sentFrames.size(), 2);
    QCOMPARE(payloadOfSentFrame(m_transport->sentFrames.at(1))
                 .value(QStringLiteral("table")).toString(),
             QStringLiteral("charger"));

    // charger 应答到达后整单完成
    feedResponse(m_transport, 200, dataPayload({}));
    QCOMPARE(readySpy.count(), 1);
    QCOMPARE(readySpy.first().at(1).value<StationPage>().items.size(), 1);
}

void RealChargerServiceTests::listAggregatesCountsFiltersAndPaginates()
{
    QSignalSpy readySpy(m_service, &IChargerService::stationsReady);

    StationQuery query;
    query.keyword = QStringLiteral("A");
    query.pageSize = 1;
    m_service->queryStations(readOnlyContext(QStringLiteral("req-page")), query);

    // 一条脏行（缺 stationName）应被跳过，不影响其余记录；
    // A站 与 A2站 都命中关键词"A"，pageSize=1 时第一页应提示还有下一页。
    QJsonObject dirty;
    dirty.insert(QStringLiteral("address"), QStringLiteral("无主站"));
    feedResponse(m_transport, 200,
                 dataPayload({stationRecord(QStringLiteral("A站"), 113.9, 22.5, 150),
                              stationRecord(QStringLiteral("A2站"), 113.95, 22.55, 160),
                              stationRecord(QStringLiteral("B站"), 114.0, 22.6, 180),
                              dirty}));
    feedResponse(m_transport, 200,
                 dataPayload({chargerRecord(QStringLiteral("CP-1"), QStringLiteral("A站"),
                                            CHARGER_IDLE, true),
                              chargerRecord(QStringLiteral("CP-2"), QStringLiteral("A站"),
                                            CHARGER_CHARGING, true),
                              chargerRecord(QStringLiteral("CP-3"), QStringLiteral("B站"),
                                            CHARGER_IDLE, true)}));

    QCOMPARE(readySpy.count(), 1);
    const StationPage page = readySpy.first().at(1).value<StationPage>();
    // 关键词"A"只命中 A站（B站不匹配）；坏行不计入
    QCOMPARE(page.items.size(), 1);
    QCOMPARE(page.items.first().name, QStringLiteral("A站"));
    QCOMPARE(page.items.first().totalCount, 2);
    QCOMPARE(page.items.first().availableCount, 1); // 充电中的不计入可用
    QCOMPARE(page.items.first().priceCentsPerKwh.value_or(-1), 150);
    QVERIFY(page.hasMore);
    QCOMPARE(page.nextCursor, QStringLiteral("1"));

    // 翻页取第二页：keyword 命中 A站/A2站，第二页应为 A2站 且 hasMore=false。
    StationQuery second;
    second.keyword = QStringLiteral("A");
    second.pageSize = 1;
    second.cursor = page.nextCursor;
    m_service->queryStations(readOnlyContext(QStringLiteral("req-page2")), second);
    feedResponse(m_transport, 200, dataPayload({stationRecord(QStringLiteral("A站"), 113.9, 22.5, 150),
                                                stationRecord(QStringLiteral("A2站"), 113.95, 22.55, 160)}));
    feedResponse(m_transport, 200, dataPayload({}));
    QCOMPARE(readySpy.count(), 2);
    const StationPage tail = readySpy.at(1).at(1).value<StationPage>();
    QCOMPARE(tail.items.size(), 1);
    QCOMPARE(tail.items.first().name, QStringLiteral("A2站"));
    QVERIFY(!tail.hasMore);
}

void RealChargerServiceTests::detailReturnsChargersOfTargetStationOnly()
{
    QSignalSpy readySpy(m_service, &IChargerService::stationDetailReady);
    m_service->queryStationDetail(readOnlyContext(QStringLiteral("req-detail")),
                                  QStringLiteral("A站"));

    feedResponse(m_transport, 200,
                 dataPayload({stationRecord(QStringLiteral("A站"), 113.9, 22.5, 150)}));
    feedResponse(m_transport, 200,
                 dataPayload({chargerRecord(QStringLiteral("CP-1"), QStringLiteral("A站"),
                                            CHARGER_IDLE, true),
                              chargerRecord(QStringLiteral("CP-2"), QStringLiteral("A站"),
                                            CHARGER_FAULT, true),
                              chargerRecord(QStringLiteral("CP-3"), QStringLiteral("B站"),
                                            CHARGER_IDLE, true)}));

    QCOMPARE(readySpy.count(), 1);
    const StationDetail detail = readySpy.first().at(1).value<StationDetail>();
    QCOMPARE(detail.stationId, QStringLiteral("A站"));
    QCOMPARE(detail.chargers.size(), 2); // B站电桩不得串入
    QCOMPARE(detail.summary.availableCount, 1);
    QVERIFY(detail.chargers.first().canStartCharging);
    QCOMPARE(detail.chargers.last().disabledReason, QStringLiteral("故障"));
    QVERIFY(detail.summary.point.has_value());
    QCOMPARE(detail.summary.point->latitude, 22.5);
}

void RealChargerServiceTests::detailFailsEarlyWhenStationMissing()
{
    QSignalSpy failedSpy(m_service, &IChargerService::requestFailed);
    m_service->queryStationDetail(readOnlyContext(QStringLiteral("req-miss")),
                                  QStringLiteral("幽灵站"));

    // station 表里没有目标站：不再发 charger 请求，快速失败可恢复。
    feedResponse(m_transport, 200,
                 dataPayload({stationRecord(QStringLiteral("A站"), 113.9, 22.5, 150)}));
    QCOMPARE(m_transport->sentFrames.size(), 1);
    QCOMPARE(failedSpy.count(), 1);
    const ClientError error = failedSpy.first().at(0).value<ClientError>();
    QCOMPARE(error.code, QStringLiteral("charger-station-not-found"));
    QCOMPARE(error.requestId, QStringLiteral("req-miss"));
    QVERIFY(!error.retryable);
}

void RealChargerServiceTests::serverErrorFailsPendingWithBizCode()
{
    QSignalSpy failedSpy(m_service, &IChargerService::requestFailed);
    m_service->queryStations(readOnlyContext(QStringLiteral("req-db")), StationQuery{});
    feedResponse(m_transport, DB_ERROR,
                 QJsonObject{{QStringLiteral("code"), QStringLiteral("DB_ERROR")},
                             {QStringLiteral("err"), QStringLiteral("database down")}});

    QCOMPARE(failedSpy.count(), 1);
    const ClientError error = failedSpy.first().at(0).value<ClientError>();
    QCOMPARE(error.code, QStringLiteral("DB_ERROR"));
    QVERIFY(error.retryable);
    QCOMPARE(error.requestId, QStringLiteral("req-db"));
}

void RealChargerServiceTests::malformedDataFailsWithBadResponse()
{
    QSignalSpy failedSpy(m_service, &IChargerService::requestFailed);
    m_service->queryStations(readOnlyContext(QStringLiteral("req-bad")), StationQuery{});
    feedResponse(m_transport, 200,
                 QJsonObject{{QStringLiteral("data"), QStringLiteral("not-an-array")}});

    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.first().at(0).value<ClientError>().code,
             QStringLiteral("bad-response"));
}

void RealChargerServiceTests::timeoutFailsWithRequestTimeout()
{
    m_service->setRequestTimeoutMs(50);
    QSignalSpy failedSpy(m_service, &IChargerService::requestFailed);
    m_service->queryStations(readOnlyContext(QStringLiteral("req-t")), StationQuery{});
    QTest::qWait(200);

    QCOMPARE(failedSpy.count(), 1);
    const ClientError error = failedSpy.first().at(0).value<ClientError>();
    QCOMPARE(error.code, QStringLiteral("request-timeout"));
    QVERIFY(error.retryable);
}

void RealChargerServiceTests::disconnectFailsPendingAsConnectionLost()
{
    QSignalSpy failedSpy(m_service, &IChargerService::requestFailed);
    m_service->queryStations(readOnlyContext(QStringLiteral("req-lost")), StationQuery{});
    emit m_transport->disconnected();

    QCOMPARE(failedSpy.count(), 1);
    const ClientError error = failedSpy.first().at(0).value<ClientError>();
    QCOMPARE(error.code, QStringLiteral("connection-lost"));
    QVERIFY(error.retryable);
}

void RealChargerServiceTests::cancelDropsLateResponses()
{
    QSignalSpy readySpy(m_service, &IChargerService::stationsReady);
    QSignalSpy failedSpy(m_service, &IChargerService::requestFailed);
    m_service->queryStations(readOnlyContext(QStringLiteral("req-c")), StationQuery{});
    m_service->cancel(QStringLiteral("req-c"));

    // 迟到的两步应答都不得产生任何信号（在途关联已摘除）。
    feedResponse(m_transport, 200, dataPayload({stationRecord(QStringLiteral("A站"), 113.9, 22.5, 150)}));
    feedResponse(m_transport, 200, dataPayload({}));
    QCOMPARE(readySpy.count(), 0);
    QCOMPARE(failedSpy.count(), 0);

    // 取消后在途已清空：同一 requestId 可以再次发起。
    m_service->queryStations(readOnlyContext(QStringLiteral("req-c")), StationQuery{});
    feedResponse(m_transport, 200, dataPayload({}));
    feedResponse(m_transport, 200, dataPayload({}));
    QCOMPARE(readySpy.count(), 1);
}

void RealChargerServiceTests::concurrentQueryIsRejectedUntilFirstSettles()
{
    QSignalSpy failedSpy(m_service, &IChargerService::requestFailed);
    QSignalSpy readySpy(m_service, &IChargerService::stationsReady);

    m_service->queryStations(readOnlyContext(QStringLiteral("req-a")), StationQuery{});
    m_service->queryStationDetail(readOnlyContext(QStringLiteral("req-b")),
                                  QStringLiteral("A站"));
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.first().at(0).value<ClientError>().code,
             QStringLiteral("charger-request-in-flight"));

    // 第一个查询完成后，被拒的第二个请求需要调用方重试（协议无法区分并发应答）。
    feedResponse(m_transport, 200, dataPayload({stationRecord(QStringLiteral("A站"), 113.9, 22.5, 150)}));
    feedResponse(m_transport, 200, dataPayload({}));
    QCOMPARE(readySpy.count(), 1);
    QCOMPARE(failedSpy.count(), 1);
}

void RealChargerServiceTests::readonlyAndInvalidInputsAreRejectedLocally()
{
    QSignalSpy failedSpy(m_service, &IChargerService::requestFailed);
    QCOMPARE(m_transport->sentFrames.size(), 0);

    // 变更语义上下文（带 operationId）不符合只读查询合同。
    RequestContext mutation{QStringLiteral("req-m"), QStringLiteral("op-1")};
    m_service->queryStations(mutation, StationQuery{});
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.first().at(0).value<ClientError>().code,
             QStringLiteral("charger-readonly-operation"));

    // 页大小越界本地拒绝，不发网络帧。
    StationQuery badPage;
    badPage.pageSize = 0;
    m_service->queryStations(readOnlyContext(QStringLiteral("req-p")), badPage);
    QCOMPARE(failedSpy.count(), 2);
    QCOMPARE(failedSpy.at(1).at(0).value<ClientError>().code,
             QStringLiteral("charger-invalid-page-size"));

    // 空站点 ID 本地拒绝。
    m_service->queryStationDetail(readOnlyContext(QStringLiteral("req-e")), QString());
    QCOMPARE(failedSpy.count(), 3);
    QCOMPARE(failedSpy.at(2).at(0).value<ClientError>().code,
             QStringLiteral("charger-invalid-station-id"));

    QCOMPARE(m_transport->sentFrames.size(), 0);
}

void RealChargerServiceTests::priceInYuanIsRoundedToCents()
{
    QSignalSpy readySpy(m_service, &IChargerService::stationsReady);
    m_service->queryStations(readOnlyContext(QStringLiteral("req-yuan")), StationQuery{});

    // 服务端只给"元"字段时四舍五入到分：1.5 元 → 150 分。
    QJsonObject yuanPriced = stationRecord(QStringLiteral("A站"), 113.9, 22.5, 150);
    yuanPriced.remove(QStringLiteral("priceCents"));
    yuanPriced.insert(QStringLiteral("price"), 1.5);
    feedResponse(m_transport, 200, dataPayload({yuanPriced}));
    feedResponse(m_transport, 200, dataPayload({}));

    QCOMPARE(readySpy.count(), 1);
    const StationPage page = readySpy.first().at(1).value<StationPage>();
    QCOMPARE(page.items.first().priceCentsPerKwh.value_or(-1), 150);
}

QTEST_GUILESS_MAIN(RealChargerServiceTests)

#include "real-charger-service-tests.moc"
