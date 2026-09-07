#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include "app/application.h"
#include "app/mapuibinder.h"
#include "demo/mapdemofixtureloader.h"
#include "demo/userdemocontroller.h"
#include "modules/charger/mockchargerservice.h"
#include "modules/map/mockmapservice.h"
#include "modules/map/tencentmapservice.h"
#include "modules/user/mockusernetworkapi.h"
#include "presentation/pages/auth/loginwindow.h"
#include "presentation/pages/shell/mainwindow.h"
#include "presentation/pages/profile/profileeditwindow.h"

namespace {

QJsonObject loadTencentMapConfig()
{
    QStringList candidates;
    const QString explicitPath = qEnvironmentVariable("CHARGING_TENCENT_CONFIG");
    if (!explicitPath.trimmed().isEmpty())
        candidates.append(explicitPath);
    candidates.append(QDir::current().filePath(
        QStringLiteral("config/tencent-map.local.json")));
    candidates.append(QDir(QCoreApplication::applicationDirPath()).filePath(
        QStringLiteral("../config/tencent-map.local.json")));

    for (const QString &path : candidates) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error == QJsonParseError::NoError && document.isObject())
            return document.object();
    }
    return {};
}

RouteResult makeRoute(const StationDetail &station,
                      TravelMode mode,
                      const GeoPoint &origin)
{
    RouteResult route;
    route.routeId = station.stationId
                    + (mode == TravelMode::Driving
                           ? QStringLiteral("-driving")
                           : QStringLiteral("-walking"));
    route.stationId = station.stationId;
    route.mode = mode;
    route.origin = origin;
    route.destination = *station.summary.point;
    route.polyline = {
        origin,
        {(origin.latitude + station.summary.point->latitude) / 2.0,
         (origin.longitude + station.summary.point->longitude) / 2.0},
        *station.summary.point
    };
    route.distanceMeters = mode == TravelMode::Driving ? 3200 : 2100;
    route.durationSeconds = mode == TravelMode::Driving ? 720 : 1680;
    route.steps = mode == TravelMode::Driving
                      ? QVector<RouteStep>{{QStringLiteral("沿当前道路向南行驶"), 800},
                                           {QStringLiteral("右转进入主干道"), 1900},
                                           {QStringLiteral("到达充电站入口"), 500}}
                      : QVector<RouteStep>{{QStringLiteral("沿人行道向南步行"), 600},
                                           {QStringLiteral("通过路口后继续直行"), 1100},
                                           {QStringLiteral("到达充电站入口"), 400}};
    return route;
}

template<typename Behavior>
void applyDemoBehavior(const MapDemoBehavior &source, Behavior *target)
{
    target->responseDelayMs = source.responseDelayMs;
    target->timeoutMs = source.timeoutMs;
    if (source.outcome == QStringLiteral("failure"))
        target->outcome = decltype(target->outcome)::Failure;
    else if (source.outcome == QStringLiteral("no-response"))
        target->outcome = decltype(target->outcome)::NoResponse;
    target->error.code = source.errorCode;
    target->error.displayMessage = source.errorMessage;
    target->error.retryable = source.retryable;
}

MapDemoFixture configureMapDemo(MockChargerService *chargerService,
                                MockMapService *mapService)
{
    MapDemoFixture fixture;
    QString fixtureError;
    if (!loadMapDemoFixture(QStringLiteral(":/demo/map-demo-data.tmp"),
                            &fixture, &fixtureError)) {
        qWarning().noquote() << fixtureError;
        chargerService->setStationCatalog({});
        fixture.canvasState = QStringLiteral("error");
        return fixture;
    }
    chargerService->setStationCatalog(fixture.stations);
    mapService->setLocationResult(fixture.location);
    MockMapService::Behavior locationBehavior;
    applyDemoBehavior(fixture.locationBehavior, &locationBehavior);
    mapService->setLocateBehavior(locationBehavior);
    MockChargerService::Behavior stationsBehavior;
    applyDemoBehavior(fixture.stationsBehavior, &stationsBehavior);
    chargerService->setStationsBehavior(stationsBehavior);
    GeocodeResult shenzhenNorth;
    shenzhenNorth.candidates = {
        {QStringLiteral("shenzhen-north-east"),
         QStringLiteral("深圳北站东广场"),
         QStringLiteral("深圳市龙华区民治街道深圳北站东广场"),
         {22.609900, 114.035700}},
        {QStringLiteral("shenzhen-north-west"),
         QStringLiteral("深圳北站西广场"),
         QStringLiteral("深圳市龙华区致远中路深圳北站西广场"),
         {22.610800, 114.029900}}
    };
    mapService->setGeocodeResult(QStringLiteral("深圳北站"), shenzhenNorth);

    for (const StationDetail &station : fixture.stations) {
        mapService->setRouteResult(station.stationId, TravelMode::Driving,
                                   makeRoute(station, TravelMode::Driving,
                                             fixture.location.point));
        mapService->setRouteResult(station.stationId, TravelMode::Walking,
                                   makeRoute(station, TravelMode::Walking,
                                             fixture.location.point));
    }
    return fixture;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("智充用户流程 Demo"));
    app.setStyle(QStringLiteral("Fusion"));

    QFile theme(QStringLiteral(":/styles/theme.qss"));
    if (theme.open(QIODevice::ReadOnly)) {
        app.setStyleSheet(QString::fromUtf8(theme.readAll()));
    }

    MockUserNetworkApi network;
    UserApplicationAssembly assembly(&network);
    MockChargerService chargerService;
    MockMapService mockMapService;
    TencentMapService tencentMapService;
    const MapDemoFixture mapFixture = configureMapDemo(&chargerService,
                                                       &mockMapService);

    const QJsonObject localMapConfig = loadTencentMapConfig();
    QString provider = qEnvironmentVariable("CHARGING_MAP_PROVIDER");
    if (provider.isEmpty())
        provider = localMapConfig.value(QStringLiteral("provider")).toString(
            QStringLiteral("mock"));
    IMapService *mapService = &mockMapService;
    if (provider.compare(QStringLiteral("tencent"), Qt::CaseInsensitive) == 0) {
        QString apiKey = qEnvironmentVariable("TENCENT_MAP_KEY");
        if (apiKey.isEmpty())
            apiKey = localMapConfig.value(QStringLiteral("key")).toString();
        QString region = qEnvironmentVariable("TENCENT_MAP_REGION");
        if (region.isEmpty())
            region = localMapConfig.value(QStringLiteral("region")).toString(
                QStringLiteral("深圳市"));
        tencentMapService.setApiKey(apiKey);
        tencentMapService.setSearchRegion(region);
        mapService = &tencentMapService;
    }
    MapUiBinder mapBinder(&chargerService, mapService);

    LoginWindow login;
    ProfileEditWindow profileEdit;
    MainWindow mainWindow;
    UserDemoController controller(&network, assembly.userUiBinder(), &mapBinder,
                                  &login, &profileEdit, &mainWindow);
    if (mapFixture.canvasState == QStringLiteral("error"))
        mapBinder.mapLoadFailed();
    else if (mapFixture.canvasState == QStringLiteral("ready"))
        mapBinder.mapReady();
    controller.showInitialPage();

    return app.exec();
}
