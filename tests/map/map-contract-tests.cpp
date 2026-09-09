#include "app/imapuibinder.h"
#include "modules/charger/ichargerservice.h"
#include "modules/map/imapservice.h"

#include <QSignalSpy>
#include <QtTest>

class ContractMapService final : public IMapService
{
public:
    using IMapService::IMapService;

    void locate(const RequestContext &context) override
    {
        LocationResult result;
        result.point = {22.543096, 114.057865};
        result.source = LocationSource::Device;
        result.capturedAtUtc = QDateTime::currentDateTimeUtc();
        emit locationReady(context, result);
    }

    void geocode(const RequestContext &context, const QString &address) override
    {
        GeocodeResult result;
        result.candidates.append({QStringLiteral("origin-1"),
                                  address,
                                  address,
                                  {22.543096, 114.057865}});
        emit geocodeReady(context, result);
    }

    void planRoute(const RequestContext &context,
                   const RouteQuery &query) override
    {
        RouteResult result;
        result.routeId = QStringLiteral("route-1");
        result.stationId = query.stationId;
        result.mode = query.mode;
        result.origin = query.origin;
        result.destination = query.destination;
        result.polyline = {query.origin, query.destination};
        result.distanceMeters = 1200;
        result.durationSeconds = 600;
        emit routeReady(context, result);
    }

    void cancel(const QString &) override {}
};

class ContractChargerService final : public IChargerService
{
public:
    using IChargerService::IChargerService;

    void queryStations(const RequestContext &context,
                       const StationQuery &) override
    {
        StationSummary station;
        station.stationId = QStringLiteral("station-1");
        station.name = QStringLiteral("Contract Station");
        station.address = QStringLiteral("Contract Address");
        station.point = GeoPoint{22.543096, 114.057865};
        station.availableCount = 1;
        station.totalCount = 2;

        StationPage page;
        page.items.append(station);
        emit stationsReady(context, page);
    }

    void queryStationDetail(const RequestContext &context,
                            const QString &stationId) override
    {
        StationDetail detail;
        detail.stationId = stationId;
        detail.summary.stationId = stationId;
        detail.updatedAtUtc = QDateTime::currentDateTimeUtc();
        emit stationDetailReady(context, detail);
    }

    void cancel(const QString &) override {}
};

class MapContractTests final : public QObject
{
    Q_OBJECT

private slots:
    void geoPointValidationUsesOptionalForUnknown()
    {
        QVERIFY(!GeoPoint{}.isValid());
        QVERIFY((GeoPoint{22.543096, 114.057865}.isValid()));
        QVERIFY((!GeoPoint{91.0, 114.0}.isValid()));
    }

    void stationQueryRequiresOneArea()
    {
        StationQuery query;
        QVERIFY(!query.hasExactlyOneArea());
        QVERIFY(query.hasAtMostOneArea());
        query.center = GeoPoint{22.543096, 114.057865};
        QVERIFY(query.hasExactlyOneArea());
        QVERIFY(query.hasAtMostOneArea());
        query.bounds = GeoBounds{{22.0, 113.0}, {23.0, 115.0}};
        QVERIFY(!query.hasExactlyOneArea());
        QVERIFY(!query.hasAtMostOneArea());
    }

    void mockServicesPublishRequestContext()
    {
        qRegisterMetaType<RequestContext>();
        qRegisterMetaType<LocationResult>();
        qRegisterMetaType<StationPage>();

        ContractMapService mapService;
        ContractChargerService chargerService;
        QSignalSpy locationSpy(&mapService, &IMapService::locationReady);
        QSignalSpy stationsSpy(&chargerService, &IChargerService::stationsReady);

        const RequestContext locationContext{QStringLiteral("location-1"), {}};
        const RequestContext stationsContext{QStringLiteral("stations-1"), {}};
        StationQuery query;
        query.center = GeoPoint{22.543096, 114.057865};

        mapService.locate(locationContext);
        chargerService.queryStations(stationsContext, query);

        QCOMPARE(locationSpy.count(), 1);
        QCOMPARE(stationsSpy.count(), 1);
        QCOMPARE(locationSpy.takeFirst().at(0).value<RequestContext>().requestId,
                 locationContext.requestId);
        QCOMPARE(stationsSpy.takeFirst().at(0).value<RequestContext>().requestId,
                 stationsContext.requestId);
    }
};

QTEST_APPLESS_MAIN(MapContractTests)

#include "map-contract-tests.moc"
