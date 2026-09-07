#include "modules/charger/mockchargerservice.h"
#include "modules/map/coordinateconverter.h"
#include "modules/map/mockmapservice.h"

#include <QSignalSpy>
#include <QtTest>

namespace {

RequestContext context(const QString &requestId)
{
    return {requestId, {}};
}

StationDetail station(const QString &id,
                      const QString &name,
                      const GeoPoint &point,
                      int chargerCount = 0)
{
    StationDetail detail;
    detail.stationId = id;
    detail.summary.stationId = id;
    detail.summary.name = name;
    detail.summary.address = name + QStringLiteral(" address");
    detail.summary.point = point;
    detail.updatedAtUtc = QDateTime::currentDateTimeUtc();
    for (int index = 0; index < chargerCount; ++index) {
        ChargerSummary charger;
        charger.chargerId = id + QStringLiteral("-charger-%1").arg(index);
        detail.chargers.append(charger);
    }
    return detail;
}

RouteQuery routeQuery(const QString &stationId, TravelMode mode)
{
    RouteQuery query;
    query.stationId = stationId;
    query.mode = mode;
    query.origin = {22.543096, 114.057865};
    query.destination = {22.533320, 114.055710};
    return query;
}

} // namespace

class M2LogicTests final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<ClientError>();
        qRegisterMetaType<RequestContext>();
        qRegisterMetaType<StationPage>();
        qRegisterMetaType<StationDetail>();
        qRegisterMetaType<LocationResult>();
        qRegisterMetaType<GeocodeResult>();
        qRegisterMetaType<RouteResult>();
    }

    void stationDetailPreservesDynamicChargerCollection()
    {
        MockChargerService service;
        service.setStationCatalog({station(QStringLiteral("station-20"),
                                           QStringLiteral("Large station"),
                                           {22.54, 114.05},
                                           20)});
        QSignalSpy ready(&service, &IChargerService::stationDetailReady);

        service.queryStationDetail(context(QStringLiteral("detail-1")),
                                   QStringLiteral("station-20"));

        QTRY_COMPARE(ready.count(), 1);
        const StationDetail result = ready.takeFirst().at(1).value<StationDetail>();
        QCOMPARE(result.stationId, QStringLiteral("station-20"));
        QCOMPARE(result.chargers.size(), 20);
        QCOMPARE(result.chargers.last().chargerId,
                 QStringLiteral("station-20-charger-19"));
    }

    void stationQueryFiltersBoundsKeywordAndPaginates()
    {
        MockChargerService service;
        service.setStationCatalog({
            station(QStringLiteral("a"), QStringLiteral("Alpha"), {22.50, 114.00}),
            station(QStringLiteral("b"), QStringLiteral("Alpha South"), {22.51, 114.01}),
            station(QStringLiteral("c"), QStringLiteral("Beta"), {23.50, 115.00})
        });
        QSignalSpy ready(&service, &IChargerService::stationsReady);

        StationQuery first;
        first.bounds = GeoBounds{{22.0, 113.0}, {23.0, 115.0}};
        first.keyword = QStringLiteral("alpha");
        first.pageSize = 1;
        service.queryStations(context(QStringLiteral("stations-1")), first);
        QTRY_COMPARE(ready.count(), 1);

        const StationPage firstPage = ready.takeFirst().at(1).value<StationPage>();
        QCOMPARE(firstPage.items.size(), 1);
        QCOMPARE(firstPage.items.first().stationId, QStringLiteral("a"));
        QVERIFY(firstPage.hasMore);
        QCOMPARE(firstPage.nextCursor, QStringLiteral("1"));

        StationQuery second = first;
        second.cursor = firstPage.nextCursor;
        service.queryStations(context(QStringLiteral("stations-2")), second);
        QTRY_COMPARE(ready.count(), 1);
        const StationPage secondPage = ready.takeFirst().at(1).value<StationPage>();
        QCOMPARE(secondPage.items.first().stationId, QStringLiteral("b"));
        QVERIFY(!secondPage.hasMore);
        QVERIFY(secondPage.nextCursor.isEmpty());
    }

    void stationQueryKeepsTextOnlyResultWithoutInventingCoordinates()
    {
        MockChargerService service;
        StationDetail textOnly = station(QStringLiteral("text-only"),
                                         QStringLiteral("Text only"),
                                         {22.5, 114.0});
        textOnly.summary.point.reset();
        service.setStationCatalog({textOnly});
        QSignalSpy ready(&service, &IChargerService::stationsReady);
        StationQuery query;
        query.bounds = GeoBounds{{22.0, 113.0}, {23.0, 115.0}};

        service.queryStations(context(QStringLiteral("stations-text")), query);

        QTRY_COMPARE(ready.count(), 1);
        const StationPage page = ready.takeFirst().at(1).value<StationPage>();
        QCOMPARE(page.items.size(), 1);
        QVERIFY(!page.items.first().point.has_value());
    }

    void invalidReadonlyRequestIsRejectedWithCorrelation()
    {
        MockChargerService service;
        QSignalSpy failed(&service, &IChargerService::requestFailed);
        StationQuery query;
        query.center = GeoPoint{22.5, 114.0};

        service.queryStations({QStringLiteral("stations-invalid"),
                               QStringLiteral("unexpected-operation")},
                              query);

        QCOMPARE(failed.count(), 1);
        const ClientError error = failed.takeFirst().at(0).value<ClientError>();
        QCOMPARE(error.requestId, QStringLiteral("stations-invalid"));
        QCOMPARE(error.code, QStringLiteral("charger-readonly-operation"));
        QVERIFY(error.operationId.isEmpty());
        QVERIFY(!error.resultUnknown);
    }

    void cancellationDropsLateStationResponse()
    {
        MockChargerService service;
        service.setStationCatalog({station(QStringLiteral("a"),
                                           QStringLiteral("Alpha"),
                                           {22.5, 114.0})});
        MockChargerService::Behavior behavior;
        behavior.responseDelayMs = 40;
        behavior.timeoutMs = 200;
        service.setStationDetailBehavior(behavior);
        QSignalSpy ready(&service, &IChargerService::stationDetailReady);
        QSignalSpy failed(&service, &IChargerService::requestFailed);

        service.queryStationDetail(context(QStringLiteral("cancel-detail")),
                                   QStringLiteral("a"));
        service.cancel(QStringLiteral("cancel-detail"));
        QTest::qWait(80);

        QCOMPARE(ready.count(), 0);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(service.cancelledRequestIds(),
                 QStringList{QStringLiteral("cancel-detail")});
    }

    void timeoutWinsAndLateMapResponseIsDropped()
    {
        MockMapService service;
        LocationResult location;
        location.point = {22.5, 114.0};
        location.capturedAtUtc = QDateTime::currentDateTimeUtc();
        service.setLocationResult(location);
        MockMapService::Behavior behavior;
        behavior.responseDelayMs = 80;
        behavior.timeoutMs = 20;
        service.setLocateBehavior(behavior);
        QSignalSpy ready(&service, &IMapService::locationReady);
        QSignalSpy failed(&service, &IMapService::requestFailed);

        service.locate(context(QStringLiteral("locate-timeout")));

        QTRY_COMPARE(failed.count(), 1);
        QTest::qWait(100);
        QCOMPARE(ready.count(), 0);
        QCOMPARE(failed.count(), 1);
        const ClientError error = failed.takeFirst().at(0).value<ClientError>();
        QCOMPARE(error.requestId, QStringLiteral("locate-timeout"));
        QCOMPARE(error.code, QStringLiteral("map-timeout"));
        QVERIFY(error.retryable);
    }

    void geocodeReturnsAllStableCandidates()
    {
        MockMapService service;
        GeocodeResult configured;
        configured.candidates = {
            {QStringLiteral("candidate-1"), QStringLiteral("North gate"),
             QStringLiteral("Full address north"), {22.50, 114.00}},
            {QStringLiteral("candidate-2"), QStringLiteral("South gate"),
             QStringLiteral("Full address south"), {22.51, 114.01}}
        };
        service.setGeocodeResult(QStringLiteral("  Science Park  "), configured);
        QSignalSpy ready(&service, &IMapService::geocodeReady);

        service.geocode(context(QStringLiteral("geocode-1")),
                        QStringLiteral("science park"));

        QTRY_COMPARE(ready.count(), 1);
        const GeocodeResult result = ready.takeFirst().at(1).value<GeocodeResult>();
        QCOMPARE(result.candidates.size(), 2);
        QCOMPARE(result.candidates.at(1).candidateId,
                 QStringLiteral("candidate-2"));
    }

    void routeUsesCurrentQueryIdentityAndMode()
    {
        MockMapService service;
        RouteResult configured;
        configured.routeId = QStringLiteral("walking-route");
        configured.polyline = {{22.543096, 114.057865},
                               {22.533320, 114.055710}};
        configured.distanceMeters = 1500;
        configured.durationSeconds = 900;
        configured.steps.append({QStringLiteral("Walk south"), 300});
        service.setRouteResult(QStringLiteral("station-a"),
                               TravelMode::Walking,
                               configured);
        QSignalSpy ready(&service, &IMapService::routeReady);
        const RouteQuery query = routeQuery(QStringLiteral("station-a"),
                                            TravelMode::Walking);

        service.planRoute(context(QStringLiteral("route-1")), query);

        QTRY_COMPARE(ready.count(), 1);
        const RouteResult result = ready.takeFirst().at(1).value<RouteResult>();
        QCOMPARE(result.stationId, query.stationId);
        QCOMPARE(result.mode, TravelMode::Walking);
        QCOMPARE(result.origin.latitude, query.origin.latitude);
        QCOMPARE(result.destination.longitude, query.destination.longitude);
        QCOMPARE(result.distanceMeters, 1500);
        QCOMPARE(result.steps.size(), 1);
    }

    void missingRouteProducesEmptyCorrelatedResult()
    {
        MockMapService service;
        QSignalSpy ready(&service, &IMapService::routeReady);
        const RouteQuery query = routeQuery(QStringLiteral("station-empty"),
                                            TravelMode::Driving);

        service.planRoute(context(QStringLiteral("route-empty")), query);

        QTRY_COMPARE(ready.count(), 1);
        const QList<QVariant> arguments = ready.takeFirst();
        QCOMPARE(arguments.at(0).value<RequestContext>().requestId,
                 QStringLiteral("route-empty"));
        const RouteResult result = arguments.at(1).value<RouteResult>();
        QCOMPARE(result.stationId, QStringLiteral("station-empty"));
        QVERIFY(result.polyline.isEmpty());
    }

    void convertsWgs84ToGcj02WithoutOffsetOutsideChina()
    {
        const GeoPoint beijing = CoordinateConverter::wgs84ToGcj02(
            {39.908823, 116.397470});
        QVERIFY(qAbs(beijing.latitude - 39.910226) < 0.00002);
        QVERIFY(qAbs(beijing.longitude - 116.403714) < 0.00002);

        const GeoPoint paris{48.8566, 2.3522};
        const GeoPoint unchanged = CoordinateConverter::wgs84ToGcj02(paris);
        QCOMPARE(unchanged.latitude, paris.latitude);
        QCOMPARE(unchanged.longitude, paris.longitude);
    }
};

QTEST_GUILESS_MAIN(M2LogicTests)

#include "m2-logic-tests.moc"
