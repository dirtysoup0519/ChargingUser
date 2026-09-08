#include "app/application.h"
#include "app/charginguibinder.h"
#include "app/chargingsessionuibinder.h"
#include "app/mapuibinder.h"
#include "app/iuseruibinder.h"
#include "app/walletuibinder.h"
#include "app/settlementuibinder.h"
#include "app/reservationuibinder.h"
#include "modules/charging/chargingservice.h"
#include "network/backendclient.h"
#include "network/qtnetworktransport.h"
#include "network/realchargerservice.h"
#include "network/realchargingnetworkapi.h"
#include "network/realorderservice.h"
#include "network/realwalletnetworkapi.h"
#include "network/realreservationservice.h"
#include "network/serverpushdispatcher.h"
#include "network/realusernetworkapi.h"
#include "modules/wallet/walletservice.h"
#include "modules/user/iuserservice.h"
#include "modules/map/tencentmapservice.h"
#include "presentation/contracts/reservationviewstates.h"
#include "presentation/contracts/orderlistviewstate.h"
#include "presentation/pages/auth/loginwindow.h"
#include "presentation/pages/charging/chargeconfirmationwindow.h"
#include "presentation/pages/charging/chargingsessionwindow.h"
#include "presentation/pages/charging/paymentwindow.h"
#include "presentation/pages/charging/qrcodescannerwindow.h"
#include "presentation/pages/charging/reservationconfirmationwindow.h"
#include "presentation/pages/charging/settlementwindow.h"
#include "presentation/pages/home/navigationwindow.h"
#include "presentation/pages/home/stationdetailwindow.h"
#include "presentation/pages/profile/profileeditwindow.h"
#include "presentation/pages/profile/orderlistwindow.h"
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
#include <QMessageBox>
#include <QRegularExpression>
#include <QStringList>
#include <QUuid>

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
        QStringLiteral("ChargingUser client (real-network entry, default build)."));
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

    // 阶段 F：真实站点/电桩确认已接入；启动变更仍受幂等与结果查询能力闸门保护。
    RealChargingNetworkApi chargingNetwork(&backend);
    ChargingService chargingService(&chargingNetwork);

    // 阶段 D：真实订单查询。登录成功后自动恢复活动订单（106/214）；
    // 会话页 UI 待交付，Binder 先行承接状态（currentState 可查询）。
    RealOrderService orderService(&backend);
    ChargingSessionUiBinder sessionBinder(&orderService);
    RealWalletNetworkApi walletNetwork(&backend);
    WalletService walletService(&walletNetwork);
    WalletUiBinder walletBinder(&walletService);
    SettlementUiBinder settlementBinder(&walletService);
    RealReservationService reservationService(&backend);
    ReservationUiBinder reservationBinder(&reservationService);
    ServerPushDispatcher pushDispatcher(&backend);

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
    mainWindow.registerSecondaryPage(&stationDetail);
    mainWindow.registerSecondaryPage(&navigation);

    // 阶段 B：充电确认/钱包/预约/扫码页面接入真实对象图。
    // 预约与扫码的业务 Binder 属于阶段 J，当前页面可达并渲染诚实的失败态。
    ChargingUiBinder chargeBinder(&chargingService);
    ChargeConfirmationWindow chargeConfirmation(&mainWindow);
    WalletRechargeWindow walletRecharge(&mainWindow);
    OrderListWindow orderList(&mainWindow);
    ReservationConfirmationWindow reservationConfirmation(&mainWindow);
    QrCodeScannerWindow qrScanner(&mainWindow);
    mainWindow.registerSecondaryPage(&chargeConfirmation);
    mainWindow.registerSecondaryPage(&walletRecharge);
    mainWindow.registerSecondaryPage(&orderList);
    mainWindow.registerSecondaryPage(&reservationConfirmation);
    mainWindow.registerSecondaryPage(&qrScanner);

    // 合同 §3：会话页与结算页接入（充电启动/活动恢复到达，G/H 阶段展示载体）。
    ChargingSessionWindow sessionWindow(&mainWindow);
    SettlementWindow settlementWindow(&mainWindow);
    mainWindow.registerSecondaryPage(&sessionWindow);
    mainWindow.registerSecondaryPage(&settlementWindow);
    if (!mapKey.isEmpty()) {
        mainWindow.setMapKey(mapKey);
    }
    IUserUiBinder *binder = assembly.userUiBinder();
    IUserService *userService = assembly.userService();
    bool profileEditOpenedFromMain = false;
    enum class WalletEntryPoint { Profile, ChargeConfirmation };
    WalletEntryPoint walletEntryPoint = WalletEntryPoint::Profile;
    const auto openWallet = [&](WalletEntryPoint entryPoint) {
        walletEntryPoint = entryPoint;
        walletBinder.activate();
        mainWindow.renderSecondaryPage(&walletRecharge);
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

    // ===== 阶段 B：充电确认链路（详情 → 确认 → 钱包/会话）=====
    QObject::connect(&stationDetail, &StationDetailWindow::chargeConfirmationRequested,
                     &chargeBinder, &IChargingUiBinder::chargeConfirmationRequested);
    QObject::connect(&chargeBinder, &IChargingUiBinder::confirmationStateChanged,
                     &chargeConfirmation, &ChargeConfirmationWindow::render);
    QObject::connect(&chargeBinder, &IChargingUiBinder::confirmationPageRequested,
                     &app, [&] {
        chargeConfirmation.render(chargeBinder.currentState());
        mainWindow.renderSecondaryPage(&chargeConfirmation);
    });
    QObject::connect(&chargeBinder, &IChargingUiBinder::stationDetailPageRequested,
                     &app, [&] {
        stationDetail.render(mapBinder.currentStationDetailState());
        mainWindow.renderSecondaryPage(&stationDetail);
    });
    QObject::connect(&chargeConfirmation, &ChargeConfirmationWindow::backRequested,
                     &chargeBinder, &IChargingUiBinder::backRequested);
    QObject::connect(&chargeConfirmation,
                     &ChargeConfirmationWindow::confirmationRefreshRequested,
                     &chargeBinder, &IChargingUiBinder::confirmationRefreshRequested);
    QObject::connect(&chargeConfirmation,
                     &ChargeConfirmationWindow::startChargingRequested,
                     &chargeBinder, &IChargingUiBinder::startChargingRequested);
    // 充电会话页依赖订单 Binder（阶段 D），到达逻辑暂缓，避免死按钮误导。
    // 钱包页可达：余额来自确认页快照，充值动作属阶段 E。
    QObject::connect(&chargeBinder, &IChargingUiBinder::rechargePageRequested,
                     &app, [&] {
        openWallet(WalletEntryPoint::ChargeConfirmation);
    });
    // 修复来源：eb31164 误用不存在的 rechargePageRequested 信号，导致真实入口
    // 无法编译；ChargeConfirmationWindow 实际声明的信号是 rechargeRequested()。
    QObject::connect(&chargeConfirmation, &ChargeConfirmationWindow::rechargeRequested,
                     &app, [&] {
        openWallet(WalletEntryPoint::ChargeConfirmation);
    });
    QObject::connect(&mainWindow, &MainWindow::rechargePageRequested,
                     &app, [&] {
        openWallet(WalletEntryPoint::Profile);
    });
    QObject::connect(&mainWindow, &MainWindow::ordersPageRequested,
                     &app, [&] {
        orderList.render(OrderListViewState{{}, QStringLiteral("正在加载订单…")});
        mainWindow.renderSecondaryPage(&orderList);
        orderService.queryActiveOrders(
            {QUuid::createUuid().toString(QUuid::WithoutBraces), {}});
    });
    const auto showProfileNotice = [&](const QString &title, const QString &text) {
        QMessageBox::information(&mainWindow, title, text);
    };
    QObject::connect(&mainWindow, &MainWindow::commonStationsPageRequested,
                     &app, [&] { showProfileNotice(QStringLiteral("常用充电站"),
                                                    QStringLiteral("常用充电站功能接入中。")); });
    QObject::connect(&mainWindow, &MainWindow::helpFeedbackPageRequested,
                     &app, [&] { showProfileNotice(QStringLiteral("帮助与反馈"),
                                                    QStringLiteral("帮助与反馈功能接入中。")); });
    QObject::connect(&mainWindow, &MainWindow::aboutPageRequested,
                     &app, [&] { showProfileNotice(QStringLiteral("关于智充"),
                                                    QStringLiteral("智充实训版")); });
    QObject::connect(&stationDetail, &StationDetailWindow::chargerSelected,
                     &mapBinder, &IMapUiBinder::chargerSelected);
    QObject::connect(&orderService, &IOrderService::activeOrdersReady,
                     &app, [&](const RequestContext &, const QVector<ChargingOrder> &orders) {
        OrderListViewState state;
        for (const ChargingOrder &order : orders) {
            OrderListItemView item;
            item.businessId = order.orderId;
            item.stationId = order.stationId;
            item.chargerId = order.chargerId;
            item.type = OrderBusinessType::Charging;
            item.stationName = order.stationName;
            item.chargerCode = order.chargerCode;
            item.createdAtText = order.startedAtUtc.isValid()
                                     ? order.startedAtUtc.toLocalTime().toString(Qt::ISODate)
                                     : QStringLiteral("时间未知");
            item.amountText = QStringLiteral("¥%1").arg(order.amountCents / 100.0, 0, 'f', 2);
            item.energyText = QStringLiteral("%1 kWh").arg(order.energyKwh, 0, 'f', 2);
            item.statusText = order.status == OrderStatus::Charging
                                  ? QStringLiteral("充电中")
                                  : order.status == OrderStatus::PendingSettlement
                                        ? QStringLiteral("待结算") : QStringLiteral("进行中");
            item.statusTone = order.status == OrderStatus::Charging ? QStringLiteral("warning")
                                                                       : QStringLiteral("success");
            item.summaryText = QStringLiteral("电量 %1").arg(item.energyText);
            item.action = order.status == OrderStatus::Charging ? OrderListAction::ViewCharging
                                                                  : OrderListAction::ContinuePayment;
            item.actionText = order.status == OrderStatus::Charging ? QStringLiteral("查看")
                                                                       : QStringLiteral("去结算");
            state.orders.append(item);
        }
        state.message = state.orders.isEmpty() ? QStringLiteral("暂无进行中的订单") : QString();
        orderList.render(state);
    });
    QObject::connect(&walletRecharge, &WalletRechargeWindow::backRequested,
                     &app, [&] {
        if (walletEntryPoint == WalletEntryPoint::ChargeConfirmation) {
            mainWindow.renderSecondaryPage(&chargeConfirmation);
        } else {
            mainWindow.renderPrimaryPage(MainWindow::PrimaryPage::Profile);
        }
    });
    // 阶段 E：钱包页面使用服务端余额与流水；充值结果未知时由 Binder 锁定重试。
    QObject::connect(&walletBinder, &WalletUiBinder::stateChanged,
                     &walletRecharge, &WalletRechargeWindow::render);
    QObject::connect(&walletRecharge, &WalletRechargeWindow::rechargeRequested,
                     &walletBinder, &WalletUiBinder::rechargeRequested);
    QObject::connect(&walletBinder, &WalletUiBinder::profileRefreshRequested,
                     userService, &IUserService::refreshCurrentUser);

    // 合同 §3.1：会话页双向接线（意图 → Binder，状态 → 渲染）。
    QObject::connect(&sessionBinder,
                     &IChargingSessionUiBinder::sessionStateChanged,
                     &sessionWindow, &ChargingSessionWindow::render);
    QObject::connect(&sessionBinder,
                     &IChargingSessionUiBinder::activeSessionsStateChanged,
                     &sessionWindow, &ChargingSessionWindow::renderSessions);
    QObject::connect(&sessionWindow, &ChargingSessionWindow::refreshRequested,
                     &sessionBinder, &IChargingSessionUiBinder::refreshRequested);
    QObject::connect(&sessionWindow, &ChargingSessionWindow::stopChargingRequested,
                     &sessionBinder, &IChargingSessionUiBinder::stopChargingRequested);
    QObject::connect(&sessionWindow,
                     &ChargingSessionWindow::recoverStopResultRequested,
                     &sessionBinder, &IChargingSessionUiBinder::recoverStopResultRequested);
    QObject::connect(&sessionWindow,
                     &ChargingSessionWindow::activeSessionsRequested,
                     &sessionBinder, &IChargingSessionUiBinder::activeSessionsRequested);
    QObject::connect(&sessionWindow,
                     &ChargingSessionWindow::activeSessionSelected,
                     &sessionBinder, &IChargingSessionUiBinder::activeSessionSelected);
    QObject::connect(&sessionWindow, &ChargingSessionWindow::scanChargingRequested,
                     &app, [&] {
        mainWindow.renderSecondaryPage(&qrScanner);
    });

    // 合同 §3.2：充电启动成功 → 携带真实 orderId 进入会话页。
    QObject::connect(&chargeBinder, &IChargingUiBinder::chargingSessionRequested,
                     &app, [&](const StartChargingResult &result) {
        sessionBinder.sessionRequested(result.orderId);
        mainWindow.renderSecondaryPage(&sessionWindow);
    });

    // 合同 §3.5：结算页接线（展示 → 支付意图 → 刷新）。
    QObject::connect(&settlementBinder, &SettlementUiBinder::stateChanged,
                     &settlementWindow, &SettlementWindow::render);
    QObject::connect(&settlementWindow, &SettlementWindow::paymentRequested,
                     &app, [&](const QString &) {
        settlementBinder.payRequested();
    });
    QObject::connect(&settlementWindow, &SettlementWindow::backRequested,
                     &app, [&] {
        sessionWindow.render(sessionBinder.currentState());
        mainWindow.renderSecondaryPage(&sessionWindow);
    });
    QObject::connect(&settlementBinder, &SettlementUiBinder::orderRefreshRequested,
                     &app, [&] {
        // 结算页刷新=按当前展示订单重查权威状态，导航保持在结算页。
    });

    // 阶段 D：登录成功 → 注入身份并自动恢复活动订单（充电中/待结算）。
    QObject::connect(&network, &RealUserNetworkApi::loginSucceeded,
                     &app, [&](const LoginResult &result) {
        orderService.setIdentity(result.session.profile.userId);
        walletNetwork.setIdentity(result.session.profile.userId);
        chargingNetwork.setIdentity(result.session.profile.userId);
        reservationService.setIdentity(result.session.profile.userId);
        pushDispatcher.setIdentity(result.session.profile.userId);
        walletBinder.setAccountId(result.session.profile.userId);
        walletBinder.activate();
        RequestContext recoveryContext{
            QUuid::createUuid().toString(QUuid::WithoutBraces), {}};
        orderService.queryActiveOrder(recoveryContext);
    });
    // 活动订单存在时交由会话 Binder 拉取详情；待支付订单直达结算页（合同 §3.4）。
    QObject::connect(&orderService, &IOrderService::activeOrderReady,
                     &app, [&](const RequestContext &,
                               const std::optional<ChargingOrder> &active) {
        if (active.has_value()) {
            sessionBinder.sessionRequested(active->orderId);
            if (active->status == OrderStatus::PendingSettlement) {
                settlementBinder.showOrder(*active);
                mainWindow.renderSecondaryPage(&settlementWindow);
            }
        }
    });
    // 合同 §3.3：停止充电成功 → 携带结算单进入结算页。
    QObject::connect(&orderService, &IOrderService::chargingStopped,
                     &app, [&](const RequestContext &,
                               const StopChargingResult &result) {
        settlementBinder.showOrder(result.order);
        mainWindow.renderSecondaryPage(&settlementWindow);
    });
    QObject::connect(&orderService, &IOrderService::orderDetailReady,
                     &app, [&](const RequestContext &,
                               const ChargingOrder &order) {
        if (order.status == OrderStatus::PendingSettlement) {
            settlementBinder.showOrder(order);
            mainWindow.renderSecondaryPage(&settlementWindow);
        }
    });
    QObject::connect(&pushDispatcher, &ServerPushDispatcher::balanceChanged,
                     &walletBinder, &WalletUiBinder::activate);
    QObject::connect(&pushDispatcher, &ServerPushDispatcher::balanceChanged,
                     userService, &IUserService::refreshCurrentUser);
    QObject::connect(&pushDispatcher, &ServerPushDispatcher::paymentNotice,
                     &app, [&](const QString &orderId) {
        if (!orderId.isEmpty()) {
            orderService.queryOrderDetail(
                {QUuid::createUuid().toString(QUuid::WithoutBraces), {}}, orderId);
        }
    });
    QObject::connect(&pushDispatcher, &ServerPushDispatcher::chargingProgress,
                     &app, [&](const ChargingProgressNotice &notice) {
        if (!notice.orderId.isEmpty()) sessionBinder.sessionRequested(notice.orderId);
    });
    QObject::connect(&pushDispatcher, &ServerPushDispatcher::chargingFault,
                     &app, [&](const ChargingFaultNotice &notice) {
        if (!notice.orderId.isEmpty()) sessionBinder.sessionRequested(notice.orderId);
        orderService.queryActiveOrder(
            {QUuid::createUuid().toString(QUuid::WithoutBraces), {}});
    });

    // ===== 预约真实网络链路：页面仅负责渲染，协议与状态由 Binder/Service 承担 =====
    QObject::connect(&stationDetail,
                     &StationDetailWindow::reservationConfirmationRequested,
                     &app, [&](const QString &stationId, const QString &chargerId) {
        ReservationConfirmationViewState state = reservationBinder.currentState();
        state.stationId = stationId;
        state.chargerId = chargerId;
        state.status = ReservationConfirmationStatus::Ready;
        state.canReserve = true;
        state.durationSeconds = 7200;
        state.durationText = QStringLiteral("2 小时");
        state.depositText = QStringLiteral("¥20.00");
        state.depositPolicyText = QStringLiteral("预约保持 2 小时，过期规则由服务端结算");
        reservationConfirmation.render(state);
        mainWindow.renderSecondaryPage(&reservationConfirmation);
    });
    QObject::connect(&reservationConfirmation,
                     &ReservationConfirmationWindow::reserveRequested,
                     &reservationBinder, &ReservationUiBinder::reserveRequested);
    QObject::connect(&reservationConfirmation,
                     &ReservationConfirmationWindow::reservationRefreshRequested,
                     &reservationBinder, &ReservationUiBinder::refreshRequested);
    QObject::connect(&reservationBinder, &ReservationUiBinder::stateChanged,
                     &reservationConfirmation, &ReservationConfirmationWindow::render);
    QObject::connect(&reservationConfirmation,
                     &ReservationConfirmationWindow::backRequested,
                     &app, [&] {
        stationDetail.render(mapBinder.currentStationDetailState());
        mainWindow.renderSecondaryPage(&stationDetail);
    });
    QObject::connect(&qrScanner, &QrCodeScannerWindow::backRequested,
                     &app, [&] {
        mainWindow.renderSecondaryPage(&stationDetail);
    });

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
    walletRecharge.render(walletBinder.currentState());
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
