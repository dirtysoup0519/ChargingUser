#include "modules/map/tencentmapservice.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QtTest>

#include <cstring>

namespace {

RequestContext readOnlyContext(const QString &requestId)
{
    // 只读查询上下文：operationId 必须为空（合同要求）。
    return {requestId, {}};
}

RouteQuery drivingQuery(const QString &stationId)
{
    RouteQuery query;
    query.stationId = stationId;
    query.mode = TravelMode::Driving;
    query.origin = {22.540000, 113.930000};
    query.destination = {22.550000, 113.940000};
    return query;
}

/**
 * 受控 QNetworkReply 替身：不真正联网，内容与失败由测试注入。
 * abort() 语义与真实实现对齐：立即以取消错误收尾。
 */
class FakeReply final : public QNetworkReply
{
public:
    FakeReply(const QNetworkRequest &request,
              const QByteArray &body,
              QNetworkReply::NetworkError error = QNetworkReply::NoError,
              const QString &errorText = {})
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        m_body = body;
        if (error != QNetworkReply::NoError) {
            setError(error, errorText);
        }
        open(QIODevice::ReadOnly);
    }

    void finishNow()
    {
        // 与真实应答一致：先报错误（若有），再发 finished 终态信号。
        if (m_finished) {
            return;
        }
        m_finished = true;
        setFinished(true);
        if (error() != QNetworkReply::NoError) {
            emit errorOccurred(error());
        }
        emit finished();
    }

    void abort() override
    {
        m_aborted = true;
        finishNow();
    }

    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 count = qMin<qint64>(maxSize, m_body.size() - m_offset);
        if (count > 0) {
            std::memcpy(data, m_body.constData() + m_offset, count);
        }
        m_offset += count;
        return count;
    }

    qint64 bytesAvailable() const override
    {
        return (m_body.size() - m_offset) + QNetworkReply::bytesAvailable();
    }

    bool m_aborted = false;

private:
    QByteArray m_body;
    qint64 m_offset = 0;
    bool m_finished = false;
};

/**
 * 受控 QNetworkAccessManager 替身：按工厂函数生成 FakeReply，并记录请求。
 */
class StubNetworkAccessManager final : public QNetworkAccessManager
{
public:
    using Factory = std::function<FakeReply *(const QNetworkRequest &)>;

    explicit StubNetworkAccessManager(QObject *parent = nullptr)
        : QNetworkAccessManager(parent)
    {
    }

    QNetworkReply *createRequest(Operation operation,
                                 const QNetworkRequest &request,
                                 QIODevice *) override
    {
        ++requestCount;
        lastOperation = operation;
        lastUrl = request.url();
        FakeReply *reply = factory ? factory(request) : nullptr;
        createdReplies.append(reply);
        return reply;
    }

    Factory factory;
    int requestCount = 0;
    Operation lastOperation = GetOperation;
    QUrl lastUrl;
    QVector<FakeReply *> createdReplies;
};

} // namespace

class TencentMapServiceTests final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // 信号参数要在 QSignalSpy 中按值存储，先注册全部元类型。
        qRegisterMetaType<RequestContext>();
        qRegisterMetaType<ClientError>();
        qRegisterMetaType<GeocodeResult>();
        qRegisterMetaType<RouteResult>();
        qRegisterMetaType<RouteQuery>();
    }

    void init()
    {
        // 每个用例独立的替身网络与适配器，避免相互污染。
        m_nam = new StubNetworkAccessManager(this);
        m_service = new TencentMapService(m_nam, this);
        m_service->setApiKey(QStringLiteral("unit-test-key"));
        m_service->setServiceBaseUrl(QStringLiteral("https://map.test"));
    }

    void geocodeSuggestionParsesCandidatesAndUrl()
    {
        m_nam->factory = [](const QNetworkRequest &request) {
            return new FakeReply(request,
                                 QByteArrayLiteral(
                                     "{\"status\":0,\"message\":\"query ok\","
                                     "\"data\":[{\"id\":\"cand-1\",\"title\":\"创新大厦\","
                                     "\"address\":\"深圳市南山区创新大厦\","
                                     "\"location\":{\"lat\":22.54,\"lng\":113.93}}]}"));
        };

        QSignalSpy geocodeSpy(m_service, &IMapService::geocodeReady);
        QSignalSpy failedSpy(m_service, &IMapService::requestFailed);
        m_service->geocode(readOnlyContext(QStringLiteral("req-geo-1")),
                           QStringLiteral("创新大厦"));
        QCOMPARE(m_nam->requestCount, 1);
        QCOMPARE(m_nam->lastUrl.path(), QStringLiteral("/ws/place/v1/suggestion"));
        QVERIFY(m_nam->lastUrl.query().contains(QStringLiteral("keyword=%E5%88%9B%E6%96%B0%E5%A4%A7%E5%8E%A6"))
                || m_nam->lastUrl.query().contains(QStringLiteral("keyword=")));
        QVERIFY(m_nam->lastUrl.query().contains(QStringLiteral("key=unit-test-key")));
        QVERIFY(m_nam->lastUrl.query().contains(QStringLiteral("region=")));
        QVERIFY(m_nam->createdReplies.first() != nullptr);
        m_nam->createdReplies.first()->finishNow();

        QCOMPARE(failedSpy.count(), 0);
        QCOMPARE(geocodeSpy.count(), 1);
        const RequestContext context =
            geocodeSpy.first().at(0).value<RequestContext>();
        QCOMPARE(context.requestId, QStringLiteral("req-geo-1"));
        const GeocodeResult result =
            geocodeSpy.first().at(1).value<GeocodeResult>();
        QCOMPARE(result.candidates.size(), 1);
        QCOMPARE(result.candidates.first().candidateId, QStringLiteral("cand-1"));
        QCOMPARE(result.candidates.first().name, QStringLiteral("创新大厦"));
        QCOMPARE(result.candidates.first().fullAddress, QStringLiteral("深圳市南山区创新大厦"));
        QCOMPARE(result.candidates.first().point.latitude, 22.54);
        QCOMPARE(result.candidates.first().point.longitude, 113.93);
    }

    void geocodeWithoutDataFieldStillSucceeds()
    {
        m_nam->factory = [](const QNetworkRequest &request) {
            return new FakeReply(request,
                                 QByteArrayLiteral("{\"status\":0,\"message\":\"query ok\"}"));
        };
        QSignalSpy geocodeSpy(m_service, &IMapService::geocodeReady);
        QSignalSpy failedSpy(m_service, &IMapService::requestFailed);
        m_service->geocode(readOnlyContext(QStringLiteral("req-geo-2")),
                           QStringLiteral("某地"));
        m_nam->createdReplies.first()->finishNow();
        QCOMPARE(failedSpy.count(), 0);
        QCOMPARE(geocodeSpy.count(), 1);
        const GeocodeResult result =
            geocodeSpy.first().at(1).value<GeocodeResult>();
        QVERIFY(result.candidates.isEmpty());
    }

    void geocodeCandidatesWithoutValidCoordinatesAreDropped()
    {
        m_nam->factory = [](const QNetworkRequest &request) {
            return new FakeReply(request, QByteArrayLiteral(
                "{\"status\":0,\"data\":["
                "{\"id\":\"missing\",\"title\":\"无坐标\"},"
                "{\"id\":\"typed-wrong\",\"location\":{\"lat\":\"22.5\",\"lng\":114.0}},"
                "{\"id\":\"valid\",\"location\":{\"lat\":22.5,\"lng\":114.0}}]}"));
        };
        QSignalSpy geocodeSpy(m_service, &IMapService::geocodeReady);
        m_service->geocode(readOnlyContext(QStringLiteral("req-geo-invalid")),
                           QStringLiteral("测试"));
        m_nam->createdReplies.first()->finishNow();
        QCOMPARE(geocodeSpy.count(), 1);
        const GeocodeResult result = geocodeSpy.first().at(1).value<GeocodeResult>();
        QCOMPARE(result.candidates.size(), 1);
        QCOMPARE(result.candidates.first().candidateId, QStringLiteral("valid"));
    }

    void routeDrivingParsesDistanceDurationPolylineAndSteps()
    {
        const QVector<GeoPoint> polyline{
            {22.540000, 113.930000},
            {22.545000, 113.935000},
            {22.550000, 113.940000}};
        // 腾讯格式：首个坐标对为绝对值，后续值是相对前两个位置的 1e-6 度增量。
        const QByteArray payload = QByteArrayLiteral(
            "{\"status\":0,\"message\":\"query ok\","
            "\"result\":{\"routes\":[{\"distance\":1500,\"duration\":12,"
            "\"polyline\":[22.54,113.93,5000,5000,5000,5000],"
            "\"steps\":[{\"instruction\":\"沿<b>科技路</b>行驶500米\",\"distance\":500}]"
            "}]}}" );
        m_nam->factory = [&payload](const QNetworkRequest &request) {
            return new FakeReply(request, payload);
        };

        QSignalSpy routeSpy(m_service, &IMapService::routeReady);
        QSignalSpy failedSpy(m_service, &IMapService::requestFailed);
        m_service->planRoute(readOnlyContext(QStringLiteral("req-route-1")),
                             drivingQuery(QStringLiteral("station-1")));
        QCOMPARE(m_nam->lastUrl.path(), QStringLiteral("/ws/direction/v1/driving/"));
        QVERIFY(m_nam->lastUrl.query().contains(QStringLiteral("from=22.540000%2C113.930000"))
                || m_nam->lastUrl.query().contains(QStringLiteral("from=22.540000,113.930000")));
        m_nam->createdReplies.first()->finishNow();

        QCOMPARE(failedSpy.count(), 0);
        QCOMPARE(routeSpy.count(), 1);
        const RequestContext context = routeSpy.first().at(0).value<RequestContext>();
        QCOMPARE(context.requestId, QStringLiteral("req-route-1"));
        const RouteResult result = routeSpy.first().at(1).value<RouteResult>();
        QCOMPARE(result.stationId, QStringLiteral("station-1"));
        QCOMPARE(result.mode, TravelMode::Driving);
        // 供应商 duration 单位是分钟，适配器统一转换为秒。
        QCOMPARE(result.durationSeconds, 12 * 60);
        QCOMPARE(result.distanceMeters, 1500);
        QCOMPARE(result.polyline.size(), polyline.size());
        for (int index = 0; index < polyline.size(); ++index) {
            QVERIFY(qAbs(result.polyline.at(index).latitude - polyline.at(index).latitude) < 1e-6);
            QVERIFY(qAbs(result.polyline.at(index).longitude - polyline.at(index).longitude) < 1e-6);
        }
        QCOMPARE(result.steps.size(), 1);
        QCOMPARE(result.steps.first().instruction, QStringLiteral("沿科技路行驶500米"));
        QVERIFY(result.steps.first().distanceMeters.has_value());
        QCOMPARE(result.steps.first().distanceMeters.value(), 500);
    }

    void routeWalkingUsesWalkingEndpointAndKeepsMode()
    {
        m_nam->factory = [](const QNetworkRequest &request) {
            return new FakeReply(request,
                                 QByteArrayLiteral("{\"status\":0,\"message\":\"query ok\","
                                                   "\"result\":{\"routes\":[]}}"));
        };
        QSignalSpy routeSpy(m_service, &IMapService::routeReady);
        RouteQuery query = drivingQuery(QStringLiteral("station-walk"));
        query.mode = TravelMode::Walking;
        m_service->planRoute(readOnlyContext(QStringLiteral("req-route-2")), query);
        QCOMPARE(m_nam->lastUrl.path(), QStringLiteral("/ws/direction/v1/walking/"));
        m_nam->createdReplies.first()->finishNow();
        QCOMPARE(routeSpy.count(), 1);
        const RouteResult result = routeSpy.first().at(1).value<RouteResult>();
        QCOMPARE(result.mode, TravelMode::Walking);
        QVERIFY(result.polyline.isEmpty());
    }

    void routeWithNoRoutesYieldsEmptySuccess()
    {
        // "无可用路线"按成功 + 空 polyline 上报，由 Binder 映射 Empty 状态。
        m_nam->factory = [](const QNetworkRequest &request) {
            return new FakeReply(request,
                                 QByteArrayLiteral("{\"status\":0,\"message\":\"query ok\","
                                                   "\"result\":{\"routes\":[]}}"));
        };
        QSignalSpy routeSpy(m_service, &IMapService::routeReady);
        QSignalSpy failedSpy(m_service, &IMapService::requestFailed);
        m_service->planRoute(readOnlyContext(QStringLiteral("req-route-3")),
                             drivingQuery(QStringLiteral("station-empty")));
        m_nam->createdReplies.first()->finishNow();
        QCOMPARE(failedSpy.count(), 0);
        QCOMPARE(routeSpy.count(), 1);
        const RouteResult result = routeSpy.first().at(1).value<RouteResult>();
        QCOMPARE(result.stationId, QStringLiteral("station-empty"));
        QVERIFY(result.polyline.isEmpty());
        QCOMPARE(result.distanceMeters, 0);
    }

    void routeWithoutRoutesFieldFailsAsMalformedPayload()
    {
        m_nam->factory = [](const QNetworkRequest &request) {
            return new FakeReply(request,
                                 QByteArrayLiteral("{\"status\":0,\"result\":{}}"));
        };
        QSignalSpy routeSpy(m_service, &IMapService::routeReady);
        QSignalSpy failedSpy(m_service, &IMapService::requestFailed);
        m_service->planRoute(readOnlyContext(QStringLiteral("req-route-malformed")),
                             drivingQuery(QStringLiteral("station-malformed")));
        m_nam->createdReplies.first()->finishNow();
        QCOMPARE(routeSpy.count(), 0);
        QCOMPARE(failedSpy.count(), 1);
        QCOMPARE(failedSpy.first().at(0).value<ClientError>().code,
                 QStringLiteral("map-parse"));
    }

    void routeWithMalformedPolylineFailsAsMalformedPayload()
    {
        m_nam->factory = [](const QNetworkRequest &request) {
            return new FakeReply(request, QByteArrayLiteral(
                "{\"status\":0,\"result\":{\"routes\":[{"
                "\"distance\":10,\"duration\":1,\"polyline\":\"wrong-type\"}]}}"));
        };
        QSignalSpy routeSpy(m_service, &IMapService::routeReady);
        QSignalSpy failedSpy(m_service, &IMapService::requestFailed);
        m_service->planRoute(readOnlyContext(QStringLiteral("req-route-polyline")),
                             drivingQuery(QStringLiteral("station-polyline")));
        m_nam->createdReplies.first()->finishNow();
        QCOMPARE(routeSpy.count(), 0);
        QCOMPARE(failedSpy.count(), 1);
        QCOMPARE(failedSpy.first().at(0).value<ClientError>().code,
                 QStringLiteral("map-parse"));
    }

    void providerDailyQuotaIsNonRetryableFailure()
    {
        m_nam->factory = [](const QNetworkRequest &request) {
            return new FakeReply(request,
                                 QByteArrayLiteral("{\"status\":121,\"message\":\"此key每日调用量已达到上限\"}"));
        };
        QSignalSpy failedSpy(m_service, &IMapService::requestFailed);
        m_service->geocode(readOnlyContext(QStringLiteral("req-rate-1")), QStringLiteral("地址"));
        m_nam->createdReplies.first()->finishNow();
        QCOMPARE(failedSpy.count(), 1);
        const ClientError error = failedSpy.first().at(0).value<ClientError>();
        QCOMPARE(error.requestId, QStringLiteral("req-rate-1"));
        QCOMPARE(error.code, QStringLiteral("map-quota-exceeded"));
        QVERIFY(!error.retryable);
        QVERIFY(!error.resultUnknown);
    }

    void providerGenericErrorIsNonRetryableFailure()
    {
        m_nam->factory = [](const QNetworkRequest &request) {
            return new FakeReply(request,
                                 QByteArrayLiteral("{\"status\":110,\"message\":\"请求来源未被授权\"}"));
        };
        QSignalSpy failedSpy(m_service, &IMapService::requestFailed);
        m_service->geocode(readOnlyContext(QStringLiteral("req-prov-1")), QStringLiteral("地址"));
        m_nam->createdReplies.first()->finishNow();
        QCOMPARE(failedSpy.count(), 1);
        const ClientError error = failedSpy.first().at(0).value<ClientError>();
        QCOMPARE(error.code, QStringLiteral("map-auth-failed"));
        QVERIFY(!error.retryable);
        QVERIFY(error.displayMessage.contains(QStringLiteral("鉴权失败")));
    }

    void networkFailureIsRetryableFailure()
    {
        m_nam->factory = [](const QNetworkRequest &request) {
            return new FakeReply(request, QByteArrayLiteral("{}"),
                                 QNetworkReply::ConnectionRefusedError,
                                 QStringLiteral("refused"));
        };
        QSignalSpy failedSpy(m_service, &IMapService::requestFailed);
        m_service->geocode(readOnlyContext(QStringLiteral("req-net-1")), QStringLiteral("地址"));
        m_nam->createdReplies.first()->finishNow();
        QCOMPARE(failedSpy.count(), 1);
        const ClientError error = failedSpy.first().at(0).value<ClientError>();
        QCOMPARE(error.code, QStringLiteral("map-network"));
        QVERIFY(error.retryable);
    }

    void malformedPayloadFailsWithParseError()
    {
        m_nam->factory = [](const QNetworkRequest &request) {
            return new FakeReply(request, QByteArrayLiteral("<html>not json</html>"));
        };
        QSignalSpy failedSpy(m_service, &IMapService::requestFailed);
        m_service->geocode(readOnlyContext(QStringLiteral("req-parse-1")), QStringLiteral("地址"));
        m_nam->createdReplies.first()->finishNow();
        QCOMPARE(failedSpy.count(), 1);
        const ClientError error = failedSpy.first().at(0).value<ClientError>();
        QCOMPARE(error.code, QStringLiteral("map-parse"));
        QVERIFY(!error.retryable);
    }

    void timeoutFailsWithRetryableMapTimeout()
    {
        // 不调用 finishNow：模拟供应商无应答，由适配器超时兜底产生终态。
        m_service->setDefaultTimeoutMs(20);
        m_nam->factory = [](const QNetworkRequest &request) {
            return new FakeReply(request, QByteArrayLiteral("{}"));
        };
        QSignalSpy failedSpy(m_service, &IMapService::requestFailed);
        m_service->geocode(readOnlyContext(QStringLiteral("req-timeout-1")), QStringLiteral("地址"));
        QCOMPARE(m_nam->requestCount, 1);
        QTest::qWait(120);
        QCOMPARE(failedSpy.count(), 1);
        const ClientError error = failedSpy.first().at(0).value<ClientError>();
        QCOMPARE(error.code, QStringLiteral("map-timeout"));
        QVERIFY(error.retryable);
        QVERIFY(m_nam->createdReplies.first()->m_aborted);
    }

    void cancelAbortsAndSuppressesLateReply()
    {
        m_nam->factory = [](const QNetworkRequest &request) {
            return new FakeReply(request,
                                 QByteArrayLiteral("{\"status\":0,\"data\":[]}"));
        };
        QSignalSpy geocodeSpy(m_service, &IMapService::geocodeReady);
        QSignalSpy failedSpy(m_service, &IMapService::requestFailed);
        m_service->geocode(readOnlyContext(QStringLiteral("req-cancel-1")), QStringLiteral("地址"));
        m_service->cancel(QStringLiteral("req-cancel-1"));
        QVERIFY(m_nam->createdReplies.first()->m_aborted);
        // abort 触发的 finished 必须被"迟到应答"规则吞掉，不再发任何信号。
        QCOMPARE(geocodeSpy.count(), 0);
        QCOMPARE(failedSpy.count(), 0);
    }

    void lateReplyAfterCancelThenReuseRequestId()
    {
        // 取消后允许同一 requestId 复用：关联表已清理，不残留"重复请求"误判。
        m_nam->factory = [](const QNetworkRequest &request) {
            return new FakeReply(request,
                                 QByteArrayLiteral("{\"status\":0,\"data\":[]}"));
        };
        m_service->geocode(readOnlyContext(QStringLiteral("req-reuse-1")), QStringLiteral("地址"));
        m_service->cancel(QStringLiteral("req-reuse-1"));
        QSignalSpy geocodeSpy(m_service, &IMapService::geocodeReady);
        m_service->geocode(readOnlyContext(QStringLiteral("req-reuse-1")), QStringLiteral("地址"));
        QCOMPARE(m_nam->requestCount, 2);
        m_nam->createdReplies.last()->finishNow();
        QCOMPARE(geocodeSpy.count(), 1);
    }

    void duplicateRequestIdIsRejectedLocally()
    {
        m_nam->factory = [](const QNetworkRequest &request) {
            return new FakeReply(request, QByteArrayLiteral("{}"));
        };
        QSignalSpy failedSpy(m_service, &IMapService::requestFailed);
        m_service->geocode(readOnlyContext(QStringLiteral("req-dup-1")), QStringLiteral("地址"));
        m_service->geocode(readOnlyContext(QStringLiteral("req-dup-1")), QStringLiteral("地址"));
        QCOMPARE(m_nam->requestCount, 1);
        QCOMPARE(failedSpy.count(), 1);
        QCOMPARE(failedSpy.first().at(0).value<ClientError>().code,
                 QStringLiteral("map-duplicate-request"));
    }

    void missingKeyFailsFastWithoutNetworkAccess()
    {
        m_service->setApiKey(QString());
        QSignalSpy failedSpy(m_service, &IMapService::requestFailed);
        m_service->geocode(readOnlyContext(QStringLiteral("req-key-1")), QStringLiteral("地址"));
        QCOMPARE(m_nam->requestCount, 0);
        QCOMPARE(failedSpy.count(), 1);
        QCOMPARE(failedSpy.first().at(0).value<ClientError>().code,
                 QStringLiteral("map-config-missing"));
    }

    void mutationContextIsRejectedAsReadonlyViolation()
    {
        QSignalSpy failedSpy(m_service, &IMapService::requestFailed);
        RequestContext context;
        context.requestId = QStringLiteral("req-mut-1");
        context.operationId = QStringLiteral("op-1");
        m_service->geocode(context, QStringLiteral("地址"));
        QCOMPARE(m_nam->requestCount, 0);
        QCOMPARE(failedSpy.count(), 1);
        QCOMPARE(failedSpy.first().at(0).value<ClientError>().code,
                 QStringLiteral("map-readonly-operation"));
    }

    void locateIsExplicitlyUnsupported()
    {
        // 定位来源未冻结：必须返回可区分错误而不是伪定位。
        QSignalSpy failedSpy(m_service, &IMapService::requestFailed);
        m_service->locate(readOnlyContext(QStringLiteral("req-loc-1")));
        QCOMPARE(failedSpy.count(), 1);
        const ClientError error = failedSpy.first().at(0).value<ClientError>();
        QCOMPARE(error.code, QStringLiteral("map-locate-unsupported"));
        QVERIFY(!error.retryable);
    }

private:
    StubNetworkAccessManager *m_nam = nullptr;
    TencentMapService *m_service = nullptr;
};

QTEST_GUILESS_MAIN(TencentMapServiceTests)

#include "tencent-map-service-tests.moc"
