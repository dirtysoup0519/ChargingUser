#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include "app/application.h"
#include "app/mapuibinder.h"
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

StationDetail makeStation(const QString &id,
                          const QString &name,
                          const QString &address,
                          const GeoPoint &point,
                          qint64 priceCents,
                          int available,
                          int total)
{
    StationDetail detail;
    detail.stationId = id;
    detail.summary.stationId = id;
    detail.summary.name = name;
    detail.summary.address = address;
    detail.summary.point = point;
    detail.summary.priceCentsPerKwh = priceCents;
    detail.summary.availableCount = available;
    detail.summary.totalCount = total;
    detail.updatedAtUtc = QDateTime::currentDateTimeUtc();
    for (int index = 0; index < total; ++index) {
        ChargerSummary charger;
        charger.chargerId = QStringLiteral("%1-%2")
                                .arg(id)
                                .arg(index + 1, 2, 10, QLatin1Char('0'));
        charger.type = index % 2 == 0 ? QStringLiteral("直流快充")
                                      : QStringLiteral("交流慢充");
        charger.powerKw = index % 2 == 0 ? 120.0 : 7.0;
        charger.online = index != total - 1 || available == total;
        charger.businessStatus = index < available
                                     ? ChargerBusinessStatus::Idle
                                     : ChargerBusinessStatus::Charging;
        charger.canStartCharging = index < available;
        if (!charger.canStartCharging) {
            charger.disabledReason = QStringLiteral("充电桩当前不可用");
        }
        detail.chargers.append(charger);
    }
    return detail;
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

void configureMapDemo(MockChargerService *chargerService,
                      MockMapService *mapService)
{
    const GeoPoint origin{22.543096, 114.057865};
    QVector<StationDetail> stations = {
        makeStation(QStringLiteral("station-sz-civic-center"),
                    QStringLiteral("市民中心充电站"),
                    QStringLiteral("福田区福中三路市民中心"),
                    {22.543430, 114.059560}, 168, 3, 5),
        makeStation(QStringLiteral("station-futian-cbd"),
                    QStringLiteral("福田 CBD 充电站"),
                    QStringLiteral("福田区金田路 3088 号"),
                    {22.536170, 114.060100}, 152, 1, 4),
        makeStation(QStringLiteral("station-nanshan-tech-park"),
                    QStringLiteral("南山科技园充电站"),
                    QStringLiteral("南山区科技南十二路"),
                    {22.531550, 113.950660}, 135, 0, 3)
    };
    stations[0].summary.distanceMeters = 900;
    stations[1].summary.distanceMeters = 1800;
    stations[2].summary.distanceMeters = 12500;
    chargerService->setStationCatalog(stations);

    LocationResult location;
    location.point = origin;
    location.accuracyMeters = 18.0;
    location.capturedAtUtc = QDateTime::currentDateTimeUtc();
    mapService->setLocationResult(location);
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

    for (const StationDetail &station : stations) {
        mapService->setRouteResult(station.stationId, TravelMode::Driving,
                                   makeRoute(station, TravelMode::Driving, origin));
        mapService->setRouteResult(station.stationId, TravelMode::Walking,
                                   makeRoute(station, TravelMode::Walking, origin));
    }
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
    configureMapDemo(&chargerService, &mockMapService);

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
    controller.showInitialPage();

    return app.exec();
}
