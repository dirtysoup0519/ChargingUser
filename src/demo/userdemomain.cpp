#include <QApplication>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include "app/application.h"
#include "app/mapuibinder.h"
#include "app/reservationuibinder.h"
#include "demo/mapdemofixtureloader.h"
#include "demo/userdemocontroller.h"
#include "modules/charger/mockchargerservice.h"
#include "modules/map/mockmapservice.h"
#include "modules/map/tencentmapservice.h"
#include "modules/reservation/mockreservationservice.h"
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
    const int directDistance = station.summary.distanceMeters.value_or(800);
    route.distanceMeters = mode == TravelMode::Driving
                               ? qMax(300, directDistance * 13 / 10)
                               : qMax(200, directDistance);
    route.durationSeconds = mode == TravelMode::Driving
                                ? qMax(180, route.distanceMeters / 7)
                                : qMax(240, route.distanceMeters * 4 / 5);
    const int firstLeg = route.distanceMeters / 4;
    const int secondLeg = route.distanceMeters * 3 / 5;
    const int finalLeg = route.distanceMeters - firstLeg - secondLeg;
    route.steps = mode == TravelMode::Driving
                      ? QVector<RouteStep>{{QStringLiteral("驶出良乡校区周边道路"), firstLeg},
                                           {QStringLiteral("沿良乡大学城道路行驶"), secondLeg},
                                           {QStringLiteral("进入充电站停车区域"), finalLeg}}
                      : QVector<RouteStep>{{QStringLiteral("从良乡校区步行出发"), firstLeg},
                                           {QStringLiteral("沿公共人行道路前往充电站"), secondLeg},
                                           {QStringLiteral("到达充电站停车区域"), finalLeg}};
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
    GeocodeResult bitLiangxiang;
    bitLiangxiang.candidates = {
        {QStringLiteral("bit-liangxiang-main"),
         QStringLiteral("北京理工大学良乡校区"),
         QStringLiteral("北京市房山区良乡高教园区良乡东路9号院"),
         {39.731782, 116.172130}},
        {QStringLiteral("bit-liangxiang-east"),
         QStringLiteral("北京理工大学良乡校区东区"),
         QStringLiteral("北京市房山区良乡东路7号院"),
         {39.733771, 116.175246}}
    };
    mapService->setGeocodeResult(QStringLiteral("北京理工大学"), bitLiangxiang);
    mapService->setGeocodeResult(QStringLiteral("北京理工大学良乡校区"),
                                 bitLiangxiang);

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
    // Resolve the matching Qt6 helper before creating QApplication so
    // Chromium can spawn its renderer process.
    if (qEnvironmentVariableIsEmpty("QTWEBENGINEPROCESS_PATH")) {
        const QString executableDir = QFileInfo(
            QString::fromLocal8Bit(argv[0])).absolutePath();
        const QStringList candidates = {
            QDir(executableDir).filePath(QStringLiteral("QtWebEngineProcess")),
            QStringLiteral("/usr/lib/x86_64-linux-gnu/qt6/libexec/QtWebEngineProcess"),
            QStringLiteral("/usr/lib/qt6/libexec/QtWebEngineProcess")};
        for (const QString &candidate : candidates) {
            if (QFileInfo::exists(candidate)) {
                qputenv("QTWEBENGINEPROCESS_PATH", candidate.toUtf8());
                break;
            }
        }
    }
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("智充用户流程 Demo"));
    app.setStyle(QStringLiteral("Fusion"));

    // 阶段 A：启动日志必须能区分运行模式——本入口只使用本地 fixture，
    // 不会连接任何服务端；正式数据入口是 src/main.cpp（real-network 模式）。
    qInfo().noquote()
        << QStringLiteral("Starting user-demo entry (mock fixtures only; "
                          "no server connection).");

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
    QString mapApiKey;
    if (provider.compare(QStringLiteral("tencent"), Qt::CaseInsensitive) == 0) {
        QString apiKey = qEnvironmentVariable("TENCENT_MAP_KEY");
        if (apiKey.isEmpty())
            apiKey = localMapConfig.value(QStringLiteral("key")).toString();
        QString region = qEnvironmentVariable("TENCENT_MAP_REGION");
        if (region.isEmpty())
            region = localMapConfig.value(QStringLiteral("region")).toString(
                QStringLiteral("北京市"));
        tencentMapService.setApiKey(apiKey);
        mapApiKey = apiKey;
        tencentMapService.setSearchRegion(region);
        const QJsonObject locationConfig = localMapConfig.value(
            QStringLiteral("defaultLocation")).toObject();
        LocationResult defaultLocation;
        defaultLocation.point = {
            locationConfig.value(QStringLiteral("latitude")).toDouble(39.731782),
            locationConfig.value(QStringLiteral("longitude")).toDouble(116.172130)};
        defaultLocation.capturedAtUtc = QDateTime::currentDateTimeUtc();
        defaultLocation.source = LocationSource::Manual;
        tencentMapService.setFallbackLocation(defaultLocation);
        mapService = &tencentMapService;
    }
    MapUiBinder mapBinder(&chargerService, mapService);
    MockReservationService reservationService;
    ReservationDemoFixture reservationFixture;
    QString reservationFixtureError;
    if (loadReservationDemoFixture(QStringLiteral(":/demo/reservation-demo-data.tmp"),
                                   &reservationFixture, &reservationFixtureError)) {
        MockReservationService::Behavior reserveBehavior;
        reserveBehavior.delayMs = reservationFixture.responseDelayMs;
        if (reservationFixture.outcome == QStringLiteral("failure"))
            reserveBehavior.outcome = MockReservationService::Outcome::Failure;
        else if (reservationFixture.outcome == QStringLiteral("result_unknown"))
            reserveBehavior.outcome = MockReservationService::Outcome::ResultUnknown;
        reserveBehavior.error.displayMessage = QStringLiteral("预约创建失败。");
        reservationService.setReserveBehavior(reserveBehavior);

        MockReservationService::Behavior cancellationBehavior;
        cancellationBehavior.delayMs = reservationFixture.cancellationResponseDelayMs;
        if (reservationFixture.cancellationOutcome == QStringLiteral("failure"))
            cancellationBehavior.outcome = MockReservationService::Outcome::Failure;
        else if (reservationFixture.cancellationOutcome == QStringLiteral("result_unknown"))
            cancellationBehavior.outcome = MockReservationService::Outcome::ResultUnknown;
        cancellationBehavior.error.displayMessage =
            reservationFixture.cancellationFailureMessage;
        reservationService.setCancellationBehavior(cancellationBehavior);
    } else {
        qWarning().noquote() << reservationFixtureError;
    }
    ReservationUiBinder reservationBinder(&reservationService);

    LoginWindow login;
    ProfileEditWindow profileEdit;
    if (!mapApiKey.trimmed().isEmpty())
        qputenv("TENCENT_MAP_KEY", mapApiKey.toUtf8());
    MainWindow mainWindow;
    UserDemoController controller(&network, assembly.userUiBinder(), &mapBinder,
                                  &reservationBinder,
                                  &login, &profileEdit, &mainWindow);
    const bool useTencentMap = provider.compare(
        QStringLiteral("tencent"), Qt::CaseInsensitive) == 0;
    if (useTencentMap) {
        if (mapApiKey.trimmed().isEmpty())
            mapBinder.mapLoadFailed();
        else
            mainWindow.setMapKey(mapApiKey);
    } else if (mapFixture.canvasState == QStringLiteral("error")) {
        mapBinder.mapLoadFailed();
    } else if (mapFixture.canvasState == QStringLiteral("ready")) {
        mapBinder.mapReady();
    }
    controller.showInitialPage();

    return app.exec();
}
