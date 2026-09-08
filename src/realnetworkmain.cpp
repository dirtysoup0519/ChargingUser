#include "app/application.h"
#include "app/mapuibinder.h"
#include "app/iuseruibinder.h"
#include "network/backendclient.h"
#include "network/qtnetworktransport.h"
#include "network/realchargerservice.h"
#include "network/realusernetworkapi.h"
#include "modules/map/tencentmapservice.h"
#include "presentation/pages/auth/loginwindow.h"
#include "presentation/pages/charging/chargeconfirmationwindow.h"
#include "presentation/pages/charging/qrcodescannerwindow.h"
#include "presentation/pages/charging/reservationconfirmationwindow.h"
#include "presentation/pages/home/navigationwindow.h"
#include "presentation/pages/home/stationdetailwindow.h"
#include "presentation/pages/profile/profileeditwindow.h"
#include "presentation/pages/profile/walletrechargewindow.h"
#include "presentation/pages/shell/mainwindow.h"
#include "protocol.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QStringList>

namespace
{

QString connectionStateName(ConnectionState state)
{
    switch (state) {
    case ConnectionState::Disconnected:
        return QStringLiteral("disconnected");
    case ConnectionState::Connecting:
        return QStringLiteral("connecting");
    case ConnectionState::Connected:
        return QStringLiteral("connected");
    case ConnectionState::Reconnecting:
        return QStringLiteral("reconnecting");
    }
    return QStringLiteral("unknown");
}

bool validHost(const QString &host)
{
    return !host.isEmpty()
           && !host.contains(
               QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")));
}

QJsonObject loadTencentMapConfig()
{
    QStringList candidates;
    const QString explicitPath =
        qEnvironmentVariable("CHARGING_TENCENT_CONFIG").trimmed();
    if (!explicitPath.isEmpty()) {
        candidates.append(explicitPath);
    }
    candidates.append(
        QDir::current().filePath(QStringLiteral("config/tencent-map.local.json")));
    candidates.append(
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("../config/tencent-map.local.json")));

    for (const QString &path : candidates) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        QJsonParseError error;
        const QJsonDocument document =
            QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error == QJsonParseError::NoError && document.isObject()) {
            return document.object();
        }
    }
    return {};
}

void configureWebEngineProcess(const char *executablePath)
{
    if (!qEnvironmentVariableIsEmpty("QTWEBENGINEPROCESS_PATH")) {
        return;
    }
    const QString executableDir =
        QFileInfo(QString::fromLocal8Bit(executablePath)).absolutePath();
    const QStringList candidates = {
        QDir(executableDir).filePath(QStringLiteral("QtWebEngineProcess")),
        QStringLiteral("/usr/lib/x86_64-linux-gnu/qt6/libexec/QtWebEngineProcess"),
        QStringLiteral("/usr/lib/qt6/libexec/QtWebEngineProcess")};
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            qputenv("QTWEBENGINEPROCESS_PATH", candidate.toUtf8());
            return;
        }
    }
}

} // namespace

int main(int argc, char *argv[])
{
    configureWebEngineProcess(argv[0]);
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("智充"));
    app.setApplicationVersion(QStringLiteral("1.0"));
    app.setStyle(QStringLiteral("Fusion"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("ChargingUser optional real-network entry."));
    parser.addHelpOption();
    parser.addVersionOption();
    // 目标地址优先级：命令行参数 > 环境变量（CHARGER_SERVER_HOST/PORT）> 协议内置默认。
    // 环境变量面向"远程服务端"联调场景：不把 IP 写进仓库，同一构建可切换本地/远程。
    const QString envHost = qEnvironmentVariable("CHARGER_SERVER_HOST").trimmed();
    const QString envPort = qEnvironmentVariable("CHARGER_SERVER_PORT").trimmed();
    const QString defaultHost = envHost.isEmpty()
                                    ? QString::fromLatin1(SERVER_IP)
                                    : envHost;
    const QString defaultPort = envPort.isEmpty()
                                    ? QString::fromLatin1(SERVER_PORT)
                                    : envPort;
    const QCommandLineOption hostOption(
        QStringLiteral("server-host"), QStringLiteral("Backend host or IP address."),
        QStringLiteral("host"), defaultHost);
    const QCommandLineOption portOption(
        QStringLiteral("server-port"), QStringLiteral("Backend TCP port."),
        QStringLiteral("port"), defaultPort);
    parser.addOption(hostOption);
    parser.addOption(portOption);
    parser.process(app);

    if (!parser.positionalArguments().isEmpty()) {
        qCritical() << "Unexpected positional arguments.";
        return 2;
    }

    const QString host = parser.value(hostOption).trimmed();
    bool portOk = false;
    const uint portValue = parser.value(portOption).toUInt(&portOk);
    if (!validHost(host)) {
        qCritical() << "Server host must be non-empty and contain no control characters.";
        return 2;
    }
    if (!portOk || portValue == 0 || portValue > 65535) {
        qCritical() << "Server port must be an integer from 1 to 65535.";
        return 2;
    }

    QFile theme(QStringLiteral(":/styles/theme.qss"));
    if (theme.open(QIODevice::ReadOnly)) {
        app.setStyleSheet(QString::fromUtf8(theme.readAll()));
    }

    // 生命周期顺序必须保持 transport > backend > network API > application assembly。
    // 以下对象按栈逆序析构，确保所有非拥有指针在使用期间有效。
    QtNetworkTransport transport(host, static_cast<quint16>(portValue));
    BackendClient backend(&transport);
    RealUserNetworkApi network(&backend);
    UserApplicationAssembly assembly(&network);
    RealChargerService chargerService(&backend);
    TencentMapService mapService;

    const QJsonObject mapConfig = loadTencentMapConfig();
    QString mapKey = qEnvironmentVariable("TENCENT_MAP_KEY").trimmed();
    if (mapKey.isEmpty()) {
        mapKey = mapConfig.value(QStringLiteral("key")).toString().trimmed();
    }
    QString mapRegion = qEnvironmentVariable("TENCENT_MAP_REGION").trimmed();
    if (mapRegion.isEmpty()) {
        mapRegion = mapConfig.value(QStringLiteral("region"))
                        .toString(QStringLiteral("北京市"))
                        .trimmed();
    }
    mapService.setApiKey(mapKey);
    mapService.setSearchRegion(mapRegion);

    const QJsonObject locationConfig =
        mapConfig.value(QStringLiteral("defaultLocation")).toObject();
    if (!locationConfig.isEmpty()) {
        LocationResult fallback;
        fallback.point.latitude =
            locationConfig.value(QStringLiteral("latitude")).toDouble();
        fallback.point.longitude =
            locationConfig.value(QStringLiteral("longitude")).toDouble();
        fallback.capturedAtUtc = QDateTime::currentDateTimeUtc();
        fallback.source = LocationSource::Manual;
        if (fallback.point.isValid()) {
            mapService.setFallbackLocation(fallback);
        }
    }
    MapUiBinder mapBinder(&chargerService, &mapService);

    LoginWindow login;
    ProfileEditWindow profileEdit;
    MainWindow mainWindow;
    StationDetailWindow stationDetail(&mainWindow);
    NavigationWindow navigation(&mainWindow);
    QrCodeScannerWindow qrScanner(&mainWindow);
    ChargeConfirmationWindow chargeConfirmation(&mainWindow);
    ReservationConfirmationWindow reservationConfirmation(&mainWindow);
    WalletRechargeWindow walletRecharge(&mainWindow);
    mainWindow.registerSecondaryPage(&stationDetail);
    mainWindow.registerSecondaryPage(&navigation);
    mainWindow.registerSecondaryPage(&qrScanner);
    mainWindow.registerSecondaryPage(&chargeConfirmation);
    mainWindow.registerSecondaryPage(&reservationConfirmation);
    mainWindow.registerSecondaryPage(&walletRecharge);
    if (!mapKey.isEmpty()) {
        mainWindow.setMapKey(mapKey);
    }
    IUserUiBinder *binder = assembly.userUiBinder();
    bool profileEditOpenedFromMain = false;
    QString pendingStationId;
    QString pendingChargerId;

    const auto selectedCharger = [&mapBinder](const QString &chargerId) {
        const StationDetailViewState detail =
            mapBinder.currentStationDetailState();
        for (const ChargerListItemView &charger : detail.chargers) {
            if (charger.chargerId == chargerId) {
                return charger;
            }
        }
        return ChargerListItemView{};
    };

    const auto showChargeConfirmation = [&] {
        const StationDetailViewState detail =
            mapBinder.currentStationDetailState();
        const ChargerListItemView charger = selectedCharger(pendingChargerId);
        ChargeConfirmationViewState state;
        state.stationId = pendingStationId;
        state.chargerId = pendingChargerId;
        state.stationName = detail.name;
        state.stationAddress = detail.address;
        state.chargerCode = charger.chargerId;
        state.chargerTypeText = charger.title;
        state.powerText = charger.powerText;
        state.chargerStatusText = charger.statusText;
        state.energyPriceText = detail.priceText;
        state.walletBalanceText = binder->currentProfileViewState().balanceText;
        state.status = ChargeConfirmationStatus::Ready;
        state.canStart = false;
        state.canRetry = false;
        state.canRecharge = true;
        state.disabledReason = QStringLiteral("真实启动充电将在阶段 F 接入");
        chargeConfirmation.render(state);
        mainWindow.renderSecondaryPage(&chargeConfirmation);
    };

    const auto showReservationConfirmation =
        [&](const QString &stationId, const QString &chargerId) {
        pendingStationId = stationId;
        pendingChargerId = chargerId;
        const StationDetailViewState detail =
            mapBinder.currentStationDetailState();
        const ChargerListItemView charger = selectedCharger(chargerId);
        ReservationConfirmationViewState state;
        state.stationId = stationId;
        state.chargerId = chargerId;
        state.stationName = detail.name;
        state.stationAddress = detail.address;
        state.chargerCode = charger.chargerId;
        state.chargerTypeText = charger.title;
        state.powerText = charger.powerText;
        state.depositText = QStringLiteral("以服务端返回为准");
        state.durationText = QStringLiteral("15 分钟");
        state.depositPolicyText = QStringLiteral("预约能力将在阶段 J 接入");
        state.durationSeconds = 15 * 60;
        state.status = ReservationConfirmationStatus::Ready;
        state.canReserve = false;
        state.canRetry = false;
        state.disabledReason = QStringLiteral("真实预约将在阶段 J 接入");
        reservationConfirmation.render(state);
        mainWindow.renderSecondaryPage(&reservationConfirmation);
    };

    const auto showOnly = [&login, &profileEdit, &mainWindow](QWidget *target) {
        login.setVisible(target == &login);
        profileEdit.setVisible(target == &profileEdit);
        mainWindow.setVisible(target == &mainWindow);
        if (target != nullptr) {
            target->raise();
            target->activateWindow();
        }
    };

    QObject::connect(&login, &LoginWindow::loginRequested,
                     binder, &IUserUiBinder::loginRequested);
    QObject::connect(&profileEdit, &ProfileEditWindow::profileSaveRequested,
                     binder, &IUserUiBinder::profileSaveRequested);
    QObject::connect(&mainWindow, &MainWindow::logoutRequested,
                     binder, &IUserUiBinder::logoutRequested);
    QObject::connect(binder, &IUserUiBinder::loginViewStateChanged,
                     &login, &LoginWindow::render);
    QObject::connect(binder, &IUserUiBinder::profileEditViewStateChanged,
                     &profileEdit, &ProfileEditWindow::render);
    QObject::connect(binder, &IUserUiBinder::profileViewStateChanged,
                     &mainWindow, &MainWindow::renderProfile);
    QObject::connect(&mainWindow, &MainWindow::primaryPageRequested,
                     &mainWindow, &MainWindow::renderPrimaryPage);
    QObject::connect(&mainWindow, &MainWindow::locateRequested,
                     &mapBinder, &IMapUiBinder::locateRequested);
    QObject::connect(&mainWindow, &MainWindow::mapReady,
                     &mapBinder, &IMapUiBinder::mapReady);
    QObject::connect(&mainWindow, &MainWindow::mapLoadFailed,
                     &mapBinder, &IMapUiBinder::mapLoadFailed);
    QObject::connect(&mainWindow, &MainWindow::stationSearchRequested,
                     &mapBinder, &IMapUiBinder::stationSearchRequested);
    QObject::connect(&mainWindow, &MainWindow::stationSearchRetryRequested,
                     &mapBinder, &IMapUiBinder::stationSearchRetryRequested);
    QObject::connect(&mainWindow, &MainWindow::stationSearchCleared,
                     &mapBinder, &IMapUiBinder::stationSearchCleared);
    QObject::connect(&mainWindow, &MainWindow::searchAreaRequested,
                     &mapBinder, &IMapUiBinder::searchAreaRequested);
    QObject::connect(&mainWindow, &MainWindow::stationSelected,
                     &mapBinder, &IMapUiBinder::stationSelected);
    QObject::connect(&mainWindow, &MainWindow::stationDetailsRequested,
                     &mapBinder, &IMapUiBinder::stationDetailsRequested);
    QObject::connect(&stationDetail, &StationDetailWindow::backRequested,
                     &mapBinder, &IMapUiBinder::backRequested);
    QObject::connect(&stationDetail, &StationDetailWindow::stationRefreshRequested,
                     &mapBinder, &IMapUiBinder::stationRefreshRequested);
    QObject::connect(&stationDetail, &StationDetailWindow::chargerSelected,
                     &mapBinder, &IMapUiBinder::chargerSelected);
    QObject::connect(&stationDetail,
                     &StationDetailWindow::chargeConfirmationRequested,
                     &mapBinder, &IMapUiBinder::chargeConfirmationRequested);
    QObject::connect(&mapBinder,
                     &IMapUiBinder::chargeConfirmationPageRequested,
                     &app, [&](const QString &stationId,
                               const QString &chargerId) {
        pendingStationId = stationId;
        pendingChargerId = chargerId;
        ScanViewState state;
        state.expectedStationId = stationId;
        state.expectedChargerId = chargerId;
        state.chargerDisplayText = selectedCharger(chargerId).title;
        state.status = ScanStatus::Error;
        state.cameraAvailable = false;
        state.cameraPermissionGranted = false;
        state.canRetry = false;
        state.canImportImage = true;
        state.message = QStringLiteral(
            "摄像头识别尚未接入，可选择图片继续核对服务器电桩数据");
        qrScanner.render(state);
        mainWindow.renderSecondaryPage(&qrScanner);
    });
    QObject::connect(&qrScanner, &QrCodeScannerWindow::backRequested,
                     &app, [&] {
        mainWindow.renderSecondaryPage(&stationDetail);
    });
    QObject::connect(&qrScanner, &QrCodeScannerWindow::imageImportRequested,
                     &app, showChargeConfirmation);
    QObject::connect(&chargeConfirmation,
                     &ChargeConfirmationWindow::backRequested,
                     &app, [&] {
        mainWindow.renderSecondaryPage(&stationDetail);
    });
    QObject::connect(&chargeConfirmation,
                     &ChargeConfirmationWindow::confirmationRefreshRequested,
                     &app, showChargeConfirmation);
    QObject::connect(&chargeConfirmation,
                     &ChargeConfirmationWindow::rechargeRequested,
                     &app, [&] {
        walletRecharge.renderBalance(
            binder->currentProfileViewState().balanceText);
        mainWindow.renderSecondaryPage(&walletRecharge);
    });
    QObject::connect(&stationDetail,
                     &StationDetailWindow::reservationConfirmationRequested,
                     &app, showReservationConfirmation);
    QObject::connect(&reservationConfirmation,
                     &ReservationConfirmationWindow::backRequested,
                     &app, [&] {
        mainWindow.renderSecondaryPage(&stationDetail);
    });
    QObject::connect(&reservationConfirmation,
                     &ReservationConfirmationWindow::reservationRefreshRequested,
                     &app, [&] {
        showReservationConfirmation(pendingStationId, pendingChargerId);
    });
    QObject::connect(&mainWindow, &MainWindow::rechargePageRequested,
                     &app, [&] {
        walletRecharge.renderBalance(
            binder->currentProfileViewState().balanceText);
        mainWindow.renderSecondaryPage(&walletRecharge);
    });
    QObject::connect(&walletRecharge, &WalletRechargeWindow::backRequested,
                     &app, [&] {
        mainWindow.renderPrimaryPage(MainWindow::PrimaryPage::Profile);
    });
    QObject::connect(&stationDetail, &StationDetailWindow::routePreviewRequested,
                     &mapBinder, &IMapUiBinder::routePreviewRequested);
    QObject::connect(&navigation, &NavigationWindow::backRequested,
                     &mapBinder, &IMapUiBinder::backRequested);
    QObject::connect(&navigation, &NavigationWindow::routeModeRequested,
                     &mapBinder, &IMapUiBinder::routeModeRequested);
    QObject::connect(&navigation, &NavigationWindow::manualOriginRequested,
                     &mapBinder, &IMapUiBinder::manualOriginRequested);
    QObject::connect(&navigation, &NavigationWindow::originCandidateSelected,
                     &mapBinder, &IMapUiBinder::originCandidateSelected);
    QObject::connect(&navigation, &NavigationWindow::routeRetryRequested,
                     &mapBinder, &IMapUiBinder::routeRetryRequested);
    QObject::connect(&mapBinder, &IMapUiBinder::homeStateChanged,
                     &mainWindow, &MainWindow::renderHome);
    QObject::connect(&mapBinder, &IMapUiBinder::stationDetailStateChanged,
                     &stationDetail, &StationDetailWindow::render);
    QObject::connect(&mapBinder, &IMapUiBinder::navigationStateChanged,
                     &navigation, &NavigationWindow::render);
    QObject::connect(&mapBinder, &IMapUiBinder::pageRequested,
                     &app, [&](MapPageTarget target, const QString &) {
        switch (target) {
        case MapPageTarget::Home:
            mainWindow.renderPrimaryPage(MainWindow::PrimaryPage::Home);
            break;
        case MapPageTarget::StationDetail:
            stationDetail.render(mapBinder.currentStationDetailState());
            mainWindow.renderSecondaryPage(&stationDetail);
            break;
        case MapPageTarget::Navigation:
            navigation.render(mapBinder.currentNavigationState());
            mainWindow.renderSecondaryPage(&navigation);
            break;
        }
    });
    QObject::connect(&mainWindow, &MainWindow::profileEditRequested,
                     &mainWindow, [&] {
        profileEditOpenedFromMain = true;
        profileEdit.render(binder->currentProfileEditViewState());
        showOnly(&profileEdit);
    });
    QObject::connect(&profileEdit, &ProfileEditWindow::backRequested,
                     &profileEdit, [&] {
        if (profileEditOpenedFromMain) {
            showOnly(&mainWindow);
            return;
        }
        binder->logoutRequested();
    });
    QObject::connect(binder, &IUserUiBinder::navigationRequested,
                     &app, [&](NavigationTarget target) {
        switch (target) {
        case NavigationTarget::Login:
            login.render(binder->currentLoginViewState());
            showOnly(&login);
            break;
        case NavigationTarget::ProfileEdit:
            profileEditOpenedFromMain = false;
            profileEdit.render(binder->currentProfileEditViewState());
            showOnly(&profileEdit);
            break;
        case NavigationTarget::Home:
        case NavigationTarget::RestrictedHome:
            mainWindow.renderProfile(binder->currentProfileViewState());
            mainWindow.renderPrimaryPage(MainWindow::PrimaryPage::Home);
            showOnly(&mainWindow);
            mapBinder.activateHome();
            break;
        }
    });
    QObject::connect(&backend, &BackendClient::connectionStateChanged,
                     &login, [](ConnectionState state) {
        qInfo().noquote()
            << QStringLiteral("Network state: %1").arg(connectionStateName(state));
    });
    QObject::connect(&backend, &BackendClient::networkError,
                     &login, [](const QString &message) {
        qWarning().noquote() << QStringLiteral("Network error: %1").arg(message);
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     &backend, &BackendClient::shutdown);

    login.render(binder->currentLoginViewState());
    profileEdit.render(binder->currentProfileEditViewState());
    mainWindow.renderProfile(binder->currentProfileViewState());
    mainWindow.renderHome(mapBinder.currentHomeState());
    if (mapKey.isEmpty()) {
        mapBinder.mapLoadFailed();
    }
    showOnly(&login);

    qInfo().noquote()
        << QStringLiteral("Starting real-network entry for %1:%2.")
               .arg(host)
               .arg(portValue);
    backend.start();
    return app.exec();
}
