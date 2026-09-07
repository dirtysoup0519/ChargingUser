#include "app/mapuibinder.h"
#include "modules/charger/mockchargerservice.h"
#include "modules/map/mockmapservice.h"

#include <QSignalSpy>
#include <QtTest>

namespace {

StationDetail station(const QString &id,
                      const QString &name,
                      const GeoPoint &point,
                      int chargerCount = 1)
{
    StationDetail detail;
    detail.stationId = id;
    detail.summary.stationId = id;
    detail.summary.name = name;
    detail.summary.address = name + QStringLiteral(" address");
    detail.summary.point = point;
    detail.summary.availableCount = chargerCount;
    detail.summary.totalCount = chargerCount;
    detail.summary.priceCentsPerKwh = 168;
    detail.updatedAtUtc = QDateTime::currentDateTimeUtc();
    for (int index = 0; index < chargerCount; ++index) {
        ChargerSummary charger;
        charger.chargerId = id + QStringLiteral("-%1").arg(index);
        charger.type = QStringLiteral("DC");
        charger.powerKw = 120.0;
        charger.online = true;
        charger.businessStatus = ChargerBusinessStatus::Idle;
        charger.canStartCharging = true;
        detail.chargers.append(charger);
    }
    return detail;
}

RouteResult route(const QString &id)
{
    RouteResult result;
    result.routeId = id;
    result.polyline = {{22.50, 114.00}, {22.51, 114.01}};
    result.distanceMeters = 1250;
    result.durationSeconds = 610;
    result.steps = {{QStringLiteral("Continue straight"), 500},
                    {QStringLiteral("Arrive"), 750}};
    return result;
}

struct Fixture
{
    MockChargerService charger;
    MockMapService map;
    MapUiBinder binder{&charger, &map};
    StationDetail first = station(QStringLiteral("station-a"),
                                  QStringLiteral("Alpha"),
                                  {22.51, 114.01}, 3);
    StationDetail second = station(QStringLiteral("station-b"),
                                   QStringLiteral("Beta"),
                                   {22.52, 114.02}, 1);

    Fixture()
    {
        charger.setStationCatalog({first, second});
        LocationResult location;
        location.point = {22.50, 114.00};
        location.capturedAtUtc = QDateTime::currentDateTimeUtc();
        map.setLocationResult(location);
        map.setRouteResult(first.stationId, TravelMode::Driving,
                           route(QStringLiteral("drive-a")));
        map.setRouteResult(first.stationId, TravelMode::Walking,
                           route(QStringLiteral("walk-a")));
        map.setRouteResult(second.stationId, TravelMode::Driving,
                           route(QStringLiteral("drive-b")));
    }

    void activate()
    {
        binder.activateHome();
        QTRY_COMPARE(binder.currentHomeState().stationsStatus,
                     MapLoadStatus::Ready);
    }
};

} // namespace

class MapUiBinderTests final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<MapPageTarget>();
        qRegisterMetaType<HomeMapViewState>();
        qRegisterMetaType<StationDetailViewState>();
        qRegisterMetaType<NavigationViewState>();
    }

    void activationLocatesThenLoadsDynamicStations()
    {
        Fixture fixture;
        QSignalSpy states(&fixture.binder, &IMapUiBinder::homeStateChanged);

        fixture.activate();

        QVERIFY(states.count() >= 3);
        const HomeMapViewState state = fixture.binder.currentHomeState();
        QCOMPARE(state.locationStatus, MapLoadStatus::Ready);
        QCOMPARE(state.stations.size(), 2);
        QCOMPARE(state.markers.size(), 2);
        QVERIFY(state.currentLocation.has_value());
    }

    void latestStationDetailWinsDuringRapidSwitch()
    {
        Fixture fixture;
        fixture.activate();
        MockChargerService::Behavior behavior;
        behavior.responseDelayMs = 40;
        behavior.timeoutMs = 200;
        fixture.charger.setStationDetailBehavior(behavior);
        QSignalSpy details(&fixture.binder,
                           &IMapUiBinder::stationDetailStateChanged);

        fixture.binder.stationDetailsRequested(fixture.first.stationId);
        fixture.binder.stationDetailsRequested(fixture.second.stationId);

        QTRY_COMPARE(fixture.binder.currentStationDetailState().status,
                     MapLoadStatus::Ready);
        const StationDetailViewState state =
            fixture.binder.currentStationDetailState();
        QCOMPARE(state.stationId, fixture.second.stationId);
        QCOMPARE(state.name, fixture.second.summary.name);
        QCOMPARE(state.chargers.size(), 1);
        QVERIFY(details.count() >= 3);
    }

    void detailRouteModeAndBackPreserveStationContext()
    {
        Fixture fixture;
        fixture.activate();
        fixture.binder.stationDetailsRequested(fixture.first.stationId);
        QTRY_COMPARE(fixture.binder.currentStationDetailState().status,
                     MapLoadStatus::Ready);
        QSignalSpy pages(&fixture.binder, &IMapUiBinder::pageRequested);

        fixture.binder.routePreviewRequested(TravelMode::Driving);
        QTRY_COMPARE(fixture.binder.currentNavigationState().routeStatus,
                     MapLoadStatus::Ready);
        QCOMPARE(fixture.binder.currentNavigationState().route->routeId,
                 QStringLiteral("drive-a"));

        fixture.binder.routeModeRequested(TravelMode::Walking);
        QTRY_COMPARE(fixture.binder.currentNavigationState().route->routeId,
                     QStringLiteral("walk-a"));
        QCOMPARE(fixture.binder.currentNavigationState().mode,
                 TravelMode::Walking);

        fixture.binder.backRequested();
        QVERIFY(!pages.isEmpty());
        const QList<QVariant> lastPage = pages.last();
        QCOMPARE(lastPage.at(0).value<MapPageTarget>(),
                 MapPageTarget::StationDetail);
        QCOMPARE(lastPage.at(1).toString(), fixture.first.stationId);
        QCOMPARE(fixture.binder.currentStationDetailState().stationId,
                 fixture.first.stationId);
    }

    void manualOriginRequiresExplicitCandidateSelection()
    {
        Fixture fixture;
        fixture.activate();
        fixture.binder.stationDetailsRequested(fixture.first.stationId);
        QTRY_COMPARE(fixture.binder.currentStationDetailState().status,
                     MapLoadStatus::Ready);
        fixture.binder.routePreviewRequested(TravelMode::Driving);
        QTRY_COMPARE(fixture.binder.currentNavigationState().routeStatus,
                     MapLoadStatus::Ready);

        GeocodeResult result;
        result.candidates = {
            {QStringLiteral("north"), QStringLiteral("North"),
             QStringLiteral("North address"), {22.60, 114.03}},
            {QStringLiteral("south"), QStringLiteral("South"),
             QStringLiteral("South address"), {22.61, 114.04}}
        };
        fixture.map.setGeocodeResult(QStringLiteral("station"), result);

        fixture.binder.manualOriginRequested(QStringLiteral(" station "));
        QTRY_COMPARE(fixture.binder.currentNavigationState().originCandidates.size(),
                     2);
        QCOMPARE(fixture.binder.currentNavigationState().routeStatus,
                 MapLoadStatus::Idle);

        fixture.binder.originCandidateSelected(QStringLiteral("south"));
        QTRY_COMPARE(fixture.binder.currentNavigationState().routeStatus,
                     MapLoadStatus::Ready);
        QCOMPARE(fixture.binder.currentNavigationState().originText,
                 QStringLiteral("South address"));
        QVERIFY(fixture.binder.currentNavigationState().originCandidates.isEmpty());
    }

    void staleRouteResultCannotOverwriteNewMode()
    {
        Fixture fixture;
        fixture.activate();
        fixture.binder.stationDetailsRequested(fixture.first.stationId);
        QTRY_COMPARE(fixture.binder.currentStationDetailState().status,
                     MapLoadStatus::Ready);
        MockMapService::Behavior behavior;
        behavior.responseDelayMs = 50;
        behavior.timeoutMs = 200;
        fixture.map.setRouteBehavior(behavior);

        fixture.binder.routePreviewRequested(TravelMode::Driving);
        // Loading 时 UI 禁止切换；模拟新语义请求需要等待当前请求可切换并不符合合同，
        // 因此用返回详情再重新进入的页面世代场景验证迟到隔离。
        fixture.binder.backRequested();
        fixture.binder.routePreviewRequested(TravelMode::Walking);

        QTRY_COMPARE(fixture.binder.currentNavigationState().routeStatus,
                     MapLoadStatus::Ready);
        QCOMPARE(fixture.binder.currentNavigationState().mode,
                 TravelMode::Walking);
        QCOMPARE(fixture.binder.currentNavigationState().route->routeId,
                 QStringLiteral("walk-a"));
    }
};

QTEST_GUILESS_MAIN(MapUiBinderTests)

#include "map-ui-binder-tests.moc"
