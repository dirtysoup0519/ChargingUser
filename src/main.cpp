#include "app/application.h"
#include "common/clienterror.h"
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
#ifdef CHARGINGUSER_USER_DEMO
#include "demo/mapdemofixtureloader.h"
#include "demo/reservationdemofixtureloader.h"
#include "modules/charger/mockchargerservice.h"
#include "modules/charging/mockchargingservice.h"
#include "modules/map/mockmapservice.h"
#include "modules/order/mockorderservice.h"
#include "modules/reservation/mockreservationservice.h"
#include "modules/user/mockusernetworkapi.h"
#include "modules/wallet/mockwalletservice.h"
#endif
#include "presentation/contracts/reservationviewstates.h"
#include "presentation/contracts/frequentstationviewstate.h"
#include "presentation/contracts/orderdetailviewstate.h"
#include "presentation/contracts/orderlistviewstate.h"
#include "presentation/contracts/paymentviewstates.h"
#include "presentation/pages/auth/loginwindow.h"
#include "presentation/pages/charging/chargeconfirmationwindow.h"
#include "presentation/pages/charging/chargingsessionwindow.h"
#include "presentation/pages/charging/paymentwindow.h"
#include "presentation/pages/charging/qrcodescannerwindow.h"
#include "presentation/pages/charging/reservationconfirmationwindow.h"
#include "presentation/pages/charging/settlementwindow.h"
#include "presentation/pages/home/navigationwindow.h"
#include "presentation/pages/home/stationdetailwindow.h"
#include "presentation/pages/profile/frequentstationswindow.h"
#include "presentation/pages/profile/orderdetailwindow.h"
#include "presentation/pages/profile/profileeditwindow.h"
#include "presentation/pages/profile/orderlistwindow.h"
#include "presentation/pages/profile/passwordchangewindow.h"
#include "presentation/pages/profile/profiletextwindow.h"
#include "presentation/pages/profile/walletrechargewindow.h"
#include "presentation/widgets/common/avatarimagehelper.h"
#include "presentation/pages/shell/mainwindow.h"

#include <QApplication>
#include <optional>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMessageBox>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QImage>
#include <QLineEdit>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QStringList>
#include <QUuid>

#include <algorithm>
#include <limits>

#ifdef CHARGINGUSER_ENABLE_ZXING
#include <ZXing/BarcodeFormat.h>
#include <ZXing/ImageView.h>
#include <ZXing/ReadBarcode.h>
#include <ZXing/ReaderOptions.h>
#endif

namespace
{

QString decodeQrImage(const QString &path, QString *error)
{
#ifdef CHARGINGUSER_ENABLE_ZXING
    QImage image(path);
    if (image.isNull()) {
        *error = QStringLiteral("无法读取所选图片。");
        return {};
    }
    image = image.convertToFormat(QImage::Format_Grayscale8);
    const ZXing::ImageView view(image.constBits(), image.width(), image.height(),
                                ZXing::ImageFormat::Lum, image.bytesPerLine());
    ZXing::ReaderOptions options;
    options.setFormats(ZXing::BarcodeFormat::QRCode);
    options.setTryHarder(true);
    const ZXing::Barcode barcode = ZXing::ReadBarcode(view, options);
    if (!barcode.isValid()) {
        *error = QStringLiteral("图片中没有识别到有效二维码。");
        return {};
    }
    return QString::fromStdString(barcode.text());
#else
    Q_UNUSED(path)
    *error = QStringLiteral("当前构建未检测到 ZXing 二维码解析库。");
    return {};
#endif
}

QString chargerCodeFromQr(const QString &raw)
{
    const QString value = raw.trimmed();
    if (value.isEmpty() || value.size() > 512) return {};
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(value.toUtf8(), &parseError);
    if (parseError.error == QJsonParseError::NoError && json.isObject()) {
        return json.object().value(QStringLiteral("chargerCode")).toString().trimmed();
    }
    const QUrl url(value);
    if (url.isValid() && !url.scheme().isEmpty()) {
        const QString code = QUrlQuery(url).queryItemValue(QStringLiteral("chargerCode")).trimmed();
        if (!code.isEmpty()) return code;
    }
    static const QRegularExpression safeCode(QStringLiteral("^[A-Za-z0-9_.:-]{1,64}$"));
    return safeCode.match(value).hasMatch() ? value : QString();
}

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

void enableWidgetInputMethods(QWidget *root)
{
    if (!root)
        return;
    for (QLineEdit *edit : root->findChildren<QLineEdit *>()) {
        if (!edit->isReadOnly())
            edit->setAttribute(Qt::WA_InputMethodEnabled, true);
    }
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

void configureWebEngineDiagnostics()
{
    const QByteArray current = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    if (qEnvironmentVariableIsEmpty("CHARGING_TENCENT_DISABLE_GPU")
        && !current.contains("--disable-gpu")) {
        return;
    }
    QByteArray flags = current;
    // --disable-gpu 会连 WebGL 一并禁用，不能与腾讯 GL 地图共存。
    flags.replace("--disable-software-rasterizer", "");
    flags.replace("--disable-gpu", "");
    flags.replace("--use-gl=swiftshader", "");
    flags.replace("--use-gl=angle", "");
    flags.replace("--use-angle=swiftshader", "");
    // Qt6 WebEngine/Chromium 在虚拟机中通过 ANGLE 调用 SwiftShader 软件 WebGL。
    flags += " --use-gl=angle --use-angle=swiftshader"
             " --enable-unsafe-swiftshader --ignore-gpu-blocklist";
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags.trimmed());
    qInfo() << "Tencent map diagnostics: SwiftShader WebGL rendering enabled.";
}

QString orderStatusText(OrderStatus status)
{
    switch (status) {
    case OrderStatus::Charging: return QStringLiteral("充电中");
    case OrderStatus::PendingSettlement: return QStringLiteral("待结算");
    case OrderStatus::Settled: return QStringLiteral("已支付");
    case OrderStatus::Cancelled: return QStringLiteral("已取消");
    case OrderStatus::Unknown: return QStringLiteral("状态未知");
    }
    return QStringLiteral("状态未知");
}

QString orderStatusTone(OrderStatus status)
{
    if (status == OrderStatus::Charging) return QStringLiteral("warning");
    if (status == OrderStatus::Settled) return QStringLiteral("success");
    return QStringLiteral("neutral");
}

QString orderDurationText(const ChargingOrder &order)
{
    if (!order.startedAtUtc.isValid()) return {};
    const QDateTime end = order.endedAtUtc.value_or(QDateTime::currentDateTimeUtc());
    const qint64 seconds = qMax<qint64>(0, order.startedAtUtc.secsTo(end));
    return QStringLiteral("%1 分钟").arg(seconds / 60);
}

OrderDetailViewState orderDetailViewState(const ChargingOrder &order)
{
    OrderDetailViewState state;
    state.businessId = order.orderId;
    state.relatedBusinessId = order.orderId;
    state.stationId = order.stationId;
    state.chargerId = order.chargerId;
    state.type = OrderBusinessType::Charging;
    state.titleText = QStringLiteral("充电订单");
    state.stationName = order.stationName;
    state.chargerCode = order.chargerCode;
    state.createdAtText = order.startedAtUtc.isValid()
                              ? order.startedAtUtc.toLocalTime().toString(Qt::ISODate)
                              : QStringLiteral("时间未知");
    state.durationText = orderDurationText(order);
    state.energyText = QStringLiteral("%1 kWh").arg(order.energyKwh, 0, 'f', 2);
    state.amountText = QStringLiteral("¥%1").arg(order.amountCents / 100.0, 0, 'f', 2);
    state.paymentMethodText = QStringLiteral("钱包支付");
    state.statusText = orderStatusText(order.status);
    state.statusTone = orderStatusTone(order.status);
    if (order.status == OrderStatus::Charging) {
        state.action = OrderListAction::ViewCharging;
        state.actionText = QStringLiteral("查看充电");
        state.actionEnabled = true;
    } else if (order.status == OrderStatus::PendingSettlement) {
        state.action = OrderListAction::ContinuePayment;
        state.actionText = QStringLiteral("去结算");
        state.actionEnabled = true;
    }
    return state;
}

bool parseMoneyText(const QString &text, qint64 *cents)
{
    QString normalized = text.trimmed();
    normalized.remove(QChar(0x00A5));
    normalized.remove(QChar(0xFFE5));
    normalized.remove(QLatin1Char(','));
    static const QRegularExpression pattern(
        QStringLiteral("^(\\d+)(?:\\.(\\d{1,2}))?$"));
    const QRegularExpressionMatch match = pattern.match(normalized);
    if (!match.hasMatch()) return false;
    bool wholeOk = false;
    const qint64 whole = match.captured(1).toLongLong(&wholeOk);
    if (!wholeOk || whole > (std::numeric_limits<qint64>::max() / 100)) return false;
    QString fraction = match.captured(2);
    if (fraction.size() == 1) fraction.append(QLatin1Char('0'));
    bool fractionOk = true;
    const qint64 fractional = fraction.isEmpty() ? 0 : fraction.toLongLong(&fractionOk);
    if (!fractionOk) return false;
    *cents = whole * 100 + fractional;
    return true;
}

QString moneyText(qint64 cents)
{
    return QStringLiteral("¥%1").arg(cents / 100.0, 0, 'f', 2);
}

} // namespace

int main(int argc, char *argv[])
{
    configureWebEngineProcess(argv[0]);
    configureWebEngineDiagnostics();
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("智充"));
    app.setApplicationVersion(QStringLiteral("1.0"));
    app.setStyle(QStringLiteral("Fusion"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("ChargingUser client (real-network entry, default build)."));
    parser.addHelpOption();
    parser.addVersionOption();
    // 目标地址优先级：命令行参数 > 环境变量（CHARGER_SERVER_HOST/PORT）> 本地安全默认。
    // 环境变量面向"远程服务端"联调场景：不把 IP 写进仓库，同一构建可切换本地/远程。
    const QString envHost = qEnvironmentVariable("CHARGER_SERVER_HOST").trimmed();
    const QString envPort = qEnvironmentVariable("CHARGER_SERVER_PORT").trimmed();
    const QString defaultHost = envHost.isEmpty()
                                    ? QStringLiteral("127.0.0.1")
                                    : envHost;
    const QString defaultPort = envPort.isEmpty()
                                    ? QStringLiteral("12345")
                                    : envPort;
    const QCommandLineOption hostOption(
        QStringLiteral("server-host"), QStringLiteral("Backend host or IP address."),
        QStringLiteral("host"), defaultHost);
    const QCommandLineOption portOption(
        QStringLiteral("server-port"), QStringLiteral("Backend TCP port."),
        QStringLiteral("port"), defaultPort);
    const QCommandLineOption unsafeOperationsOption(
        QStringLiteral("allow-unsafe-test-operations"),
        QStringLiteral("TEST_ONLY: allow start/payment mutations without server result-query guarantees."));
    const QCommandLineOption safeOperationsOption(
        QStringLiteral("safe-operations-only"),
        QStringLiteral("Disable training-only start/payment mutations."));
    parser.addOption(hostOption);
    parser.addOption(portOption);
    parser.addOption(unsafeOperationsOption);
    parser.addOption(safeOperationsOption);
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
    QString activeHost = host;
    quint16 activePort = static_cast<quint16>(portValue);

    QFile theme(QStringLiteral(":/styles/theme.qss"));
    if (theme.open(QIODevice::ReadOnly)) {
        app.setStyleSheet(QString::fromUtf8(theme.readAll()));
    }

#ifdef CHARGINGUSER_USER_DEMO
    MockUserNetworkApi network;
    LoginResult demoLogin;
    demoLogin.session.authenticated = true;
    demoLogin.session.accountStatus = AccountStatus::Normal;
    demoLogin.session.profile.userId = QStringLiteral("demo-user");
    demoLogin.session.profile.phone = QStringLiteral("13800138000");
    demoLogin.session.profile.nickname = QStringLiteral("演示用户");
    QFile demoUserFile(QStringLiteral(":/demo/user-demo-data.tmp"));
    if (demoUserFile.open(QIODevice::ReadOnly)) {
        const QJsonArray users = QJsonDocument::fromJson(
            demoUserFile.readAll()).object().value(QStringLiteral("users")).toArray();
        if (!users.isEmpty()) {
            const QJsonObject user = users.first().toObject();
            demoLogin.session.profile.phone = user.value(
                QStringLiteral("phone")).toString(demoLogin.session.profile.phone);
            demoLogin.session.profile.nickname = user.value(
                QStringLiteral("nickname")).toString(demoLogin.session.profile.nickname);
        }
    }
    demoLogin.profileCompleted = true;
    network.setLoginResult(demoLogin);
    UserProfileResult demoProfile;
    demoProfile.profile = demoLogin.session.profile;
    demoProfile.accountStatus = demoLogin.session.accountStatus;
    network.setUserProfileResult(demoProfile);
    UserApplicationAssembly assembly(&network);
    MockChargerService chargerService;
    MockMapService mapService;
    MockChargingService chargingService;
    MockOrderService orderService;
    ChargingSessionUiBinder sessionBinder(&orderService);
    MockWalletService walletService;
    WalletSnapshot demoWallet;
    demoWallet.accountId = demoLogin.session.profile.userId;
    QFile demoPaymentFile(QStringLiteral(":/demo/payment-demo-data.tmp"));
    if (demoPaymentFile.open(QIODevice::ReadOnly)) {
        const QJsonObject payment = QJsonDocument::fromJson(
            demoPaymentFile.readAll()).object().value(
                QStringLiteral("paymentDemo")).toObject();
        qint64 fixtureBalance = 0;
        if (parseMoneyText(payment.value(
                QStringLiteral("walletBalanceText")).toString(), &fixtureBalance))
            demoWallet.balanceCents = fixtureBalance;
    }
    demoWallet.fetchedAtUtc = QDateTime::currentDateTimeUtc();
    walletService.setSnapshot(demoWallet);
    WalletUiBinder walletBinder(&walletService);
    SettlementUiBinder settlementBinder(&walletService);
    MockReservationService reservationService;
    ReservationUiBinder reservationBinder(&reservationService);
#else
    // 生命周期顺序必须保持 transport > backend > network API > application assembly。
    // 以下对象按栈逆序析构，确保所有非拥有指针在使用期间有效。
    QtNetworkTransport transport(host, static_cast<quint16>(portValue));
    BackendClient backend(&transport);
    RealUserNetworkApi network(&backend);
    UserApplicationAssembly assembly(&network);
    RealChargerService chargerService(&backend);
    TencentMapService mapService;

    // 阶段 F：真实站点/电桩确认已接入；启动变更仍受幂等与结果查询能力闸门保护。
    // TEST_ONLY: the training environment is enabled by default for local integration.
    // Pass --safe-operations-only only when deliberately checking read-only behavior.
    const bool trainingOperationsEnabled = !parser.isSet(safeOperationsOption);
    RealChargingNetworkApi chargingNetwork(&backend);
    chargingNetwork.setUnsafeTestOperationsEnabled(trainingOperationsEnabled);
    ChargingService chargingService(&chargingNetwork);

    // 阶段 D：真实订单查询。登录成功后自动恢复活动订单（106/214）；
    // 会话页 UI 待交付，Binder 先行承接状态（currentState 可查询）。
    RealOrderService orderService(&backend);
    ChargingSessionUiBinder sessionBinder(&orderService);
    RealWalletNetworkApi walletNetwork(&backend);
    walletNetwork.setUnsafeTestOperationsEnabled(trainingOperationsEnabled);
    WalletService walletService(&walletNetwork);
    WalletUiBinder walletBinder(&walletService);
    SettlementUiBinder settlementBinder(&walletService);
    RealReservationService reservationService(&backend);
    ReservationUiBinder reservationBinder(&reservationService);
    ServerPushDispatcher pushDispatcher(&backend);
#endif

    QString mapKey;
#ifdef CHARGINGUSER_USER_DEMO
    MapDemoFixture demoMap;
    QString demoMapError;
    if (loadMapDemoFixture(QStringLiteral(":/demo/map-demo-data.tmp"),
                           &demoMap, &demoMapError)) {
        chargerService.setStationCatalog(demoMap.stations);
        mapService.setLocationResult(demoMap.location);
        QVector<ChargeConfirmationSnapshot> snapshots;
        for (const StationDetail &station : demoMap.stations) {
            for (const ChargerSummary &charger : station.chargers) {
                ChargeConfirmationSnapshot snapshot;
                snapshot.stationId = station.stationId;
                snapshot.chargerId = charger.chargerId;
                snapshot.stationName = station.summary.name;
                snapshot.stationAddress = station.summary.address;
                snapshot.chargerCode = charger.chargerId.section(QLatin1Char('-'), -1);
                snapshot.chargerType = charger.type;
                snapshot.powerKw = charger.powerKw;
                snapshot.priceCentsPerKwh = station.summary.priceCentsPerKwh;
                snapshot.walletBalanceCents = demoWallet.balanceCents;
                snapshot.canStart = charger.online
                                    && charger.businessStatus == ChargerBusinessStatus::Idle;
                snapshot.startOperationSupported = true;
                snapshot.canRecharge = true;
                snapshots.append(snapshot);
            }
            if (station.summary.point) {
                for (TravelMode mode : {TravelMode::Driving, TravelMode::Walking}) {
                    RouteResult route;
                    route.routeId = station.stationId
                                    + (mode == TravelMode::Driving
                                           ? QStringLiteral("-driving")
                                           : QStringLiteral("-walking"));
                    route.stationId = station.stationId;
                    route.mode = mode;
                    route.origin = demoMap.location.point;
                    route.destination = *station.summary.point;
                    route.polyline = {route.origin, route.destination};
                    route.distanceMeters = station.summary.distanceMeters.value_or(800);
                    route.durationSeconds = mode == TravelMode::Driving
                        ? qMax(180, route.distanceMeters / 7)
                        : qMax(240, route.distanceMeters * 4 / 5);
                    mapService.setRouteResult(station.stationId, mode, route);
                }
            }
        }
        chargingService.setSnapshots(snapshots);
    } else {
        qWarning().noquote() << demoMapError;
    }
    ReservationDemoFixture demoReservation;
    QString demoReservationError;
    if (loadReservationDemoFixture(QStringLiteral(":/demo/reservation-demo-data.tmp"),
                                   &demoReservation, &demoReservationError)) {
        MockReservationService::Behavior reserveBehavior;
        reserveBehavior.delayMs = demoReservation.responseDelayMs;
        reservationService.setReserveBehavior(reserveBehavior);
        MockReservationService::Behavior cancelBehavior;
        cancelBehavior.delayMs = demoReservation.cancellationResponseDelayMs;
        reservationService.setCancellationBehavior(cancelBehavior);
    }
#else
    const QJsonObject mapConfig = loadTencentMapConfig();
    mapKey = qEnvironmentVariable("TENCENT_MAP_KEY").trimmed();
    if (mapKey.isEmpty()) {
        mapKey = mapConfig.value(QStringLiteral("key")).toString().trimmed();
    }
    QString mapRegion = qEnvironmentVariable("TENCENT_MAP_REGION").trimmed();
    if (mapRegion.isEmpty()) {
        mapRegion = mapConfig.value(QStringLiteral("region"))
                        .toString(QStringLiteral("北京市"))
                        .trimmed();
    }
    qInfo().noquote() << QStringLiteral("Tencent map config: key=%1, region=%2, source=%3")
                             .arg(mapKey.isEmpty() ? QStringLiteral("missing")
                                                   : QStringLiteral("present"),
                                  mapRegion,
                                  qEnvironmentVariable("TENCENT_MAP_KEY").trimmed().isEmpty()
                                      ? QStringLiteral("config/tencent-map.local.json or default")
                                      : QStringLiteral("TENCENT_MAP_KEY"));
    mapService.setApiKey(mapKey);
    mapService.setSearchRegion(mapRegion);

    const QJsonObject locationConfig =
        mapConfig.value(QStringLiteral("defaultLocation")).toObject();
    // 配置缺失时仍以北京理工大学为默认定位，避免本地配置只有 key/region
    // 时回退到地图 SDK 的未知位置。
    LocationResult fallback;
    fallback.point.latitude =
        locationConfig.value(QStringLiteral("latitude")).toDouble(39.731782);
    fallback.point.longitude =
        locationConfig.value(QStringLiteral("longitude")).toDouble(116.172130);
    fallback.capturedAtUtc = QDateTime::currentDateTimeUtc();
    fallback.source = LocationSource::Manual;
    if (fallback.point.isValid()) {
        mapService.setFallbackLocation(fallback);
    }
#endif
    MapUiBinder mapBinder(&chargerService, &mapService);
#ifdef CHARGINGUSER_USER_DEMO
    QObject::connect(&walletService, &IWalletService::moneyOperationSucceeded,
                     &app, [&](const RequestContext &, const MoneyOperationResult &result) {
        if (result.type == MoneyOperationType::PayOrder)
            orderService.markSettled(result.orderId);
    });
    QObject::connect(&reservationService, &IReservationService::reservationCreated,
                     &app, [&](const RequestContext &, const ReservationResult &) {
        walletService.debit(2000);
        walletBinder.activate();
    });
    QObject::connect(&reservationService, &IReservationService::reservationCancelled,
                     &app, [&](const RequestContext &,
                               const ReservationCancellationResult &) {
        walletService.credit(2000);
        walletBinder.activate();
    });
#endif

    LoginWindow login;
    ProfileEditWindow profileEdit;
    MainWindow mainWindow;
    StationDetailWindow stationDetail(&mainWindow);
    NavigationWindow navigation(&mainWindow);
    mainWindow.registerSecondaryPage(&stationDetail);
    mainWindow.registerSecondaryPage(&navigation);

#ifdef CHARGINGUSER_USER_DEMO
    QObject::connect(&chargingService, &IChargingService::chargingStarted,
                     &app, [&](const RequestContext &, const StartChargingResult &result) {
        ChargingOrder order;
        order.orderId = result.orderId;
        order.stationId = result.stationId;
        order.chargerId = result.chargerId;
        order.status = OrderStatus::Charging;
        order.startedAtUtc = result.startedAtUtc;
        order.priceCentsPerKwhSnapshot = result.priceCentsPerKwhSnapshot;
        order.progressPercent = 1;
        for (const StationDetail &station : demoMap.stations) {
            if (station.stationId != result.stationId) continue;
            order.stationName = station.summary.name;
            for (const ChargerSummary &charger : station.chargers) {
                if (charger.chargerId != result.chargerId) continue;
                order.chargerCode = charger.chargerId.section(QLatin1Char('-'), -1);
                order.chargerType = charger.type;
                order.ratedPowerKw = charger.powerKw;
                order.currentPowerKw = charger.powerKw.value_or(0.0) * 0.7;
                break;
            }
            break;
        }
        orderService.upsertOrder(order);
    });
#endif
    // 阶段 B：充电确认/钱包/预约/扫码页面接入真实对象图。
    // 预约与扫码的业务 Binder 属于阶段 J，当前页面可达并渲染诚实的失败态。
    ChargingUiBinder chargeBinder(&chargingService);
    ChargeConfirmationWindow chargeConfirmation(&mainWindow);
    WalletRechargeWindow walletRecharge(&mainWindow);
    OrderListWindow orderList(&mainWindow);
    OrderDetailWindow orderDetail(&mainWindow);
    FrequentStationsWindow frequentStations(&mainWindow);
    ProfileTextWindow profileText(&mainWindow);
    ReservationConfirmationWindow reservationConfirmation(&mainWindow);
    QrCodeScannerWindow qrScanner(&mainWindow);
    PaymentWindow paymentWindow(&mainWindow);
    mainWindow.registerSecondaryPage(&chargeConfirmation);
    mainWindow.registerSecondaryPage(&walletRecharge);
    mainWindow.registerSecondaryPage(&orderList);
    mainWindow.registerSecondaryPage(&orderDetail);
    mainWindow.registerSecondaryPage(&frequentStations);
    mainWindow.registerSecondaryPage(&profileText);
    mainWindow.registerSecondaryPage(&reservationConfirmation);
    mainWindow.registerSecondaryPage(&qrScanner);
    mainWindow.registerSecondaryPage(&paymentWindow);

    // 合同 §3：会话页与结算页接入（充电启动/活动恢复到达，G/H 阶段展示载体）。
    ChargingSessionWindow sessionWindow(&mainWindow);
    SettlementWindow settlementWindow(&mainWindow);
    PasswordChangeWindow passwordChange;
    enableWidgetInputMethods(&login);
    enableWidgetInputMethods(&profileEdit);
    enableWidgetInputMethods(&mainWindow);
    enableWidgetInputMethods(&navigation);
    enableWidgetInputMethods(&walletRecharge);
    enableWidgetInputMethods(&passwordChange);
    mainWindow.registerSecondaryPage(&sessionWindow);
    mainWindow.registerSecondaryPage(&settlementWindow);
    if (!mapKey.isEmpty()) {
        mainWindow.setMapKey(mapKey);
    }
    IUserUiBinder *binder = assembly.userUiBinder();
    IUserService *userService = assembly.userService();
    bool profileEditOpenedFromMain = false;
    enum class WalletEntryPoint { Profile, ChargeConfirmation, Payment };
    enum class ScanEntryPoint { PrimaryCharging, Session };
    enum class OrderDetailDestination { None, Detail, Settlement };
    ScanEntryPoint scanEntryPoint = ScanEntryPoint::PrimaryCharging;
    bool confirmationOpenedFromScanner = false;
    QString pendingOldPassword;
    QString pendingCompletionNickname;
    bool settingInitialPassword = false;
    std::optional<ActiveReservationView> activeReservation;
    QString activeUserId;
    QSettings reservationStore(QStringLiteral("ChargingUser"), QStringLiteral("ChargingUser"));
    const auto clearReservation = [&] {
        if (!activeUserId.isEmpty())
            reservationStore.remove(QStringLiteral("reservation/%1").arg(activeUserId));
        reservationBinder.consumeActiveReservation();
        activeReservation.reset();
        chargeBinder.setReservationActive(false);
        mapBinder.setActiveReservation(std::nullopt);
    };
    const auto restoreReservation = [&](const QString &userId) {
        activeUserId = userId.trimmed();
        activeReservation.reset();
        reservationStore.beginGroup(QStringLiteral("reservation/%1").arg(activeUserId));
        ActiveReservationView view;
        view.reservationId = reservationStore.value(QStringLiteral("id")).toString();
        view.stationId = reservationStore.value(QStringLiteral("station")).toString();
        view.chargerId = reservationStore.value(QStringLiteral("charger")).toString();
        view.expiresAtUtc = QDateTime::fromMSecsSinceEpoch(reservationStore.value(QStringLiteral("expires")).toLongLong(), Qt::UTC);
        reservationStore.endGroup();
        if (!view.stationId.isEmpty() && !view.chargerId.isEmpty()
            && (!view.expiresAtUtc.isValid() || view.expiresAtUtc > QDateTime::currentDateTimeUtc())) {
            view.canCancel = true;
            view.remainingText = QStringLiteral("预约已恢复");
            activeReservation = view;
        }
        mapBinder.setActiveReservation(activeReservation);
        reservationBinder.restoreActiveReservation(activeReservation);
        chargeBinder.setReservationActive(activeReservation.has_value());
        if (activeReservation)
            chargeBinder.setReservationChargerCode(activeReservation->chargerId);
        if (activeReservation)
            mapBinder.chargerStatusConfirmed(activeReservation->stationId,
                                             activeReservation->chargerId,
                                             ChargerBusinessStatus::Reserved);
    };
    const auto clearExpiredReservation = [&] {
        if (activeReservation && activeReservation->expiresAtUtc.isValid()
            && activeReservation->expiresAtUtc <= QDateTime::currentDateTimeUtc()) {
            const ActiveReservationView expired = *activeReservation;
            reservationBinder.expireReservationIfNeeded();
            activeReservation.reset();
            mapBinder.setActiveReservation(std::nullopt);
            mapBinder.chargerStatusConfirmed(expired.stationId, expired.chargerId,
                                             ChargerBusinessStatus::Idle);
        }
    };
    QTimer reservationExpiryTimer;
    QObject::connect(&reservationExpiryTimer, &QTimer::timeout,
                     &app, clearExpiredReservation);
    reservationExpiryTimer.start(30000);
    QObject::connect(&reservationBinder, &ReservationUiBinder::activeReservationChanged,
                     &app, [&](const std::optional<ActiveReservationView> &reservation) {
        const std::optional<ActiveReservationView> previous = activeReservation;
        activeReservation = reservation;
        mapBinder.setActiveReservation(reservation);
        chargeBinder.setReservationActive(reservation.has_value());
        if (reservation) {
            chargeBinder.setReservationChargerCode(reservation->chargerId);
            mapBinder.chargerStatusConfirmed(reservation->stationId,
                                             reservation->chargerId,
                                             ChargerBusinessStatus::Reserved);
        } else if (previous) {
            mapBinder.chargerStatusConfirmed(previous->stationId,
                                             previous->chargerId,
                                             ChargerBusinessStatus::Idle);
        }
        if (activeUserId.isEmpty())
            return;
        const QString key = QStringLiteral("reservation/%1").arg(activeUserId);
        if (!reservation) {
            reservationStore.remove(key);
            return;
        }
        reservationStore.beginGroup(key);
        reservationStore.setValue(QStringLiteral("id"), reservation->reservationId);
        reservationStore.setValue(QStringLiteral("station"), reservation->stationId);
        reservationStore.setValue(QStringLiteral("charger"), reservation->chargerId);
        reservationStore.setValue(QStringLiteral("expires"),
                                  reservation->expiresAtUtc.toMSecsSinceEpoch());
        reservationStore.endGroup();
    });
    const auto openStationDetails = [&](const QString &requestedStation) {
        clearExpiredReservation();
        mapBinder.stationDetailsRequested(activeReservation
                                              ? activeReservation->stationId
                                              : requestedStation);
    };
    WalletEntryPoint walletEntryPoint = WalletEntryPoint::Profile;
    bool orderListOpen = false;
    QString orderListRequestId;
    bool frequentStationsOpen = false;
    QString frequentStationsRequestId;
    bool settlementOpenedFromOrderList = false;
    bool paymentOpen = false;
    bool reservationPaymentOpen = false;
    ReservationConfirmationViewState pendingReservationPayment;
    QString pendingOrderDetailRequestId;
    QString pendingOrderDetailOrderId;
    OrderDetailDestination pendingOrderDetailDestination = OrderDetailDestination::None;
    OrderListViewState orderListBaseState;
    QVector<ReservationHistoryItem> reservationHistory;
    const auto openWallet = [&](WalletEntryPoint entryPoint) {
        walletEntryPoint = entryPoint;
        if (entryPoint == WalletEntryPoint::Payment) paymentOpen = false;
        walletBinder.activate();
        mainWindow.renderSecondaryPage(&walletRecharge);
    };
    const auto renderPayment = [&] {
        const WalletViewState wallet = walletBinder.currentState();
        PaymentViewState state;
        if (reservationPaymentOpen) {
            state.businessId = QStringLiteral("reservation:%1:%2")
                                   .arg(pendingReservationPayment.stationId,
                                        pendingReservationPayment.chargerId);
            state.purpose = PaymentPurpose::Reservation;
            state.titleText = QStringLiteral("预约支付");
            state.descriptionText = QStringLiteral("%1 · %2号桩预约押金")
                                        .arg(pendingReservationPayment.stationName,
                                             pendingReservationPayment.chargerCode);
            state.amountText = pendingReservationPayment.depositText;
            state.balanceText = wallet.balanceText;
            qint64 balanceCents = 0;
            qint64 amountCents = 0;
            const bool balanceKnown = parseMoneyText(wallet.balanceText, &balanceCents);
            const bool amountKnown = parseMoneyText(state.amountText, &amountCents);
            state.canRecharge = wallet.status == WalletPageStatus::Ready;
            state.canPay = state.canRecharge && balanceKnown && amountKnown
                           && balanceCents >= amountCents;
            if (balanceKnown && amountKnown && balanceCents >= amountCents)
                state.balanceAfterPaymentText = moneyText(balanceCents - amountCents);
            if (!state.canPay)
                state.message = wallet.status == WalletPageStatus::Ready
                    ? QStringLiteral("钱包余额不足，请先充值后再支付。")
                    : QStringLiteral("正在同步钱包余额…");
            const ReservationConfirmationViewState reservationState =
                reservationBinder.currentState();
            if (reservationState.status == ReservationConfirmationStatus::Submitting) {
                state.status = PaymentViewStatus::Submitting;
                state.canPay = false;
                state.canRecharge = false;
                state.message = QStringLiteral("正在确认预约支付结果，请勿重复提交…");
            } else if (reservationState.status == ReservationConfirmationStatus::ResultUnknown) {
                state.status = PaymentViewStatus::ResultUnknown;
                state.canPay = false;
                state.canRecharge = false;
                state.message = reservationState.message;
            } else if (reservationState.status == ReservationConfirmationStatus::Error) {
                state.status = PaymentViewStatus::Error;
                state.canPay = reservationState.canRetry && state.canPay;
                state.message = reservationState.message;
            }
            paymentWindow.render(state);
            return;
        }
        const SettlementViewState settlement = settlementBinder.currentState();
        state.businessId = settlement.orderId;
        state.purpose = PaymentPurpose::ChargingSettlement;
        state.titleText = QStringLiteral("订单支付");
        state.descriptionText = QStringLiteral("%1 · %2号桩充电订单")
                                    .arg(settlement.stationName.isEmpty()
                                             ? QStringLiteral("未知站点")
                                             : settlement.stationName,
                                         settlement.chargerCode.isEmpty()
                                             ? QStringLiteral("未知")
                                             : settlement.chargerCode);
        state.amountText = settlement.payableText.isEmpty()
                               ? settlement.amountText : settlement.payableText;
        state.balanceText = wallet.balanceText;
        state.canRecharge = wallet.status != WalletPageStatus::Loading
                            && wallet.status != WalletPageStatus::Submitting;

        qint64 balanceCents = 0;
        qint64 amountCents = 0;
        const bool balanceKnown = parseMoneyText(wallet.balanceText, &balanceCents);
        const bool amountKnown = parseMoneyText(state.amountText, &amountCents);
        if (balanceKnown && amountKnown && balanceCents >= amountCents) {
            state.balanceAfterPaymentText = moneyText(balanceCents - amountCents);
        }

        switch (settlement.status) {
        case SettlementPageStatus::Submitting:
            state.status = PaymentViewStatus::Submitting;
            state.canPay = false;
            state.canRecharge = false;
            state.message = settlement.message;
            break;
        case SettlementPageStatus::Settled:
            state.status = PaymentViewStatus::Success;
            state.canPay = false;
            state.canRecharge = false;
            state.balanceText = settlement.balanceText.isEmpty()
                                    ? wallet.balanceText : settlement.balanceText;
            state.message = settlement.message;
            break;
        case SettlementPageStatus::ResultUnknown:
            state.status = PaymentViewStatus::ResultUnknown;
            state.canPay = false;
            state.canRecharge = false;
            state.canRecoverResult = false;
            state.message = settlement.message
                            + QStringLiteral("\n服务端未提供支付结果查询接口，请刷新订单和钱包后确认，勿重复支付。");
            break;
        case SettlementPageStatus::Error:
            state.status = PaymentViewStatus::Error;
            state.canPay = false;
            state.message = settlement.message;
            break;
        case SettlementPageStatus::Ready:
            state.status = PaymentViewStatus::Ready;
            state.canPay = wallet.status == WalletPageStatus::Ready
                           && balanceKnown && amountKnown
                           && balanceCents >= amountCents;
            if (wallet.status != WalletPageStatus::Ready || !balanceKnown) {
                state.canRecharge = false;
                state.message = wallet.message.isEmpty()
                                    ? QStringLiteral("正在同步钱包余额…")
                                    : wallet.message;
            } else if (!state.canPay) {
                state.message = QStringLiteral("钱包余额不足，请先充值后再支付。");
            } else {
                state.message = settlement.message;
            }
            break;
        case SettlementPageStatus::Idle:
            state.status = PaymentViewStatus::Error;
            state.canPay = false;
            state.canRecharge = false;
            state.message = QStringLiteral("尚未加载待支付订单。");
            break;
        }
        paymentWindow.render(state);
    };

    const auto showOnly = [&login, &profileEdit, &mainWindow,
                           &passwordChange](QWidget *target) {
        login.setVisible(target == &login);
        profileEdit.setVisible(target == &profileEdit);
        mainWindow.setVisible(target == &mainWindow);
        passwordChange.setVisible(target == &passwordChange);
        if (target != nullptr) {
            target->raise();
            target->activateWindow();
        }
    };

    QObject::connect(&login, &LoginWindow::loginRequested,
                     binder, &IUserUiBinder::loginRequested);
    QObject::connect(&login, &LoginWindow::usernamePasswordLoginRequested,
                     binder, &IUserUiBinder::usernamePasswordLoginRequested);
#ifndef CHARGINGUSER_USER_DEMO
    QObject::connect(&login, &LoginWindow::serverSettingsRequested,
                     &app, [&] {
        QDialog dialog(&login);
        dialog.setWindowTitle(QStringLiteral("服务器连接设置"));
        auto *form = new QFormLayout(&dialog);
        auto *hostEdit = new QLineEdit(activeHost, &dialog);
        auto *portEdit = new QSpinBox(&dialog);
        portEdit->setRange(1, 65535);
        portEdit->setValue(activePort);
        form->addRow(QStringLiteral("IP / 主机"), hostEdit);
        form->addRow(QStringLiteral("端口"), portEdit);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                               | QDialogButtonBox::Cancel, &dialog);
        form->addRow(buttons);
        QObject::connect(buttons, &QDialogButtonBox::accepted,
                         &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected,
                         &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) return;
        const QString newHost = hostEdit->text().trimmed();
        const quint16 newPort = static_cast<quint16>(portEdit->value());
        if (!validHost(newHost)) {
            QMessageBox::warning(&login, QStringLiteral("服务器设置"),
                                 QStringLiteral("IP / 主机地址无效。"));
            return;
        }
        if (newHost == activeHost && newPort == activePort) return;
        if (!backend.switchEndpoint(newHost, newPort)) {
            QMessageBox::warning(&login, QStringLiteral("服务器设置"),
                                 QStringLiteral("无法切换服务器地址。"));
            return;
        }
        activeHost = newHost;
        activePort = newPort;
        qInfo().noquote() << QStringLiteral("Server endpoint switched to %1:%2.")
                                 .arg(activeHost).arg(activePort);
    });
#endif
    QObject::connect(&profileEdit, &ProfileEditWindow::profileSaveRequested,
                     binder, &IUserUiBinder::profileSaveRequested);
    QObject::connect(&profileEdit, &ProfileEditWindow::avatarChangeRequested,
                     &app, [&] {
        QString dataUri;
        QString error;
        QPixmap preview;
        if (!AvatarImageHelper::selectFromAlbum(&profileEdit, &dataUri,
                                                 &preview, &error)) {
            if (!error.isEmpty())
                QMessageBox::warning(&profileEdit, QStringLiteral("更换头像"), error);
            return;
        }
        profileEdit.setAvatarPreview(dataUri);
        binder->avatarUpdateRequested(dataUri);
    });
    QObject::connect(&profileEdit, &ProfileEditWindow::profileCompletionRequested,
                     &app, [&](const QString &nickname, const QString &phone,
                               const QString &newPassword) {
        if (newPassword.isEmpty()) {
            // 新用户未修改资料时，沿用服务端自动注册的手机号昵称，确保保存请求合法。
            binder->profileSaveRequested(nickname.trimmed().isEmpty()
                                             ? phone.trimmed() : nickname);
            return;
        }
        settingInitialPassword = true;
        pendingCompletionNickname = nickname.trimmed().isEmpty()
                                        ? phone.trimmed() : nickname.trimmed();
        userService->changePassword(QString(), newPassword);
    });
    QObject::connect(&profileEdit, &ProfileEditWindow::passwordChangeRequested,
                     &app, [&] {
        pendingOldPassword.clear();
        passwordChange.setGeometry(profileEdit.geometry());
        passwordChange.setStep(PasswordChangeStep::VerifyOriginal);
        showOnly(&passwordChange);
    });
    QObject::connect(&passwordChange, &PasswordChangeWindow::backRequested,
                     &app, [&] {
        pendingOldPassword.clear();
        showOnly(&profileEdit);
    });
    QObject::connect(&passwordChange, &PasswordChangeWindow::originalPasswordSubmitted,
                     &app, [&](const QString &oldPassword) {
        pendingOldPassword = oldPassword;
        QMessageBox::information(
            &passwordChange, QStringLiteral("继续修改密码"),
            QStringLiteral("原密码已记录。设置新密码并提交后，将由服务端统一验证原密码。"));
        passwordChange.setStep(PasswordChangeStep::EnterNewPassword);
    });
    QObject::connect(&passwordChange, &PasswordChangeWindow::newPasswordSubmitted,
                     &app, [&](const QString &newPassword) {
        if (pendingOldPassword.isEmpty()) {
            passwordChange.setStep(PasswordChangeStep::VerifyOriginal,
                                   QStringLiteral("请重新输入当前密码。"));
            return;
        }
        passwordChange.setSubmitting(true, QStringLiteral("正在由服务端验证并修改密码…"));
        userService->changePassword(pendingOldPassword, newPassword);
    });
    QObject::connect(userService, &IUserService::passwordChanged,
                     &app, [&](const OperationResult &) {
        if (settingInitialPassword) {
            settingInitialPassword = false;
            const QString nickname = pendingCompletionNickname;
            pendingCompletionNickname.clear();
            binder->profileSaveRequested(nickname);
            return;
        }
        pendingOldPassword.clear();
        passwordChange.setSubmitting(false);
        QMessageBox::information(&passwordChange, QStringLiteral("修改成功"),
                                 QStringLiteral("登录密码已修改，请使用新密码登录。"));
        binder->logoutRequested();
    });
    QObject::connect(userService, &IUserService::passwordChangeFailed,
                     &app, [&](const ClientError &error) {
        const QString message = error.resultUnknown
                                    ? QStringLiteral("修改结果暂时未知，请退出后分别使用新旧密码登录确认，暂勿重复提交。")
                                    : (error.code == QStringLiteral("AUTH_FAIL")
                                           ? QStringLiteral("原密码不正确，请重新输入。")
                                           : (error.displayMessage.isEmpty()
                                                  ? QStringLiteral("密码修改失败，请稍后重试。")
                                                  : error.displayMessage));
        if (settingInitialPassword) {
            settingInitialPassword = false;
            pendingCompletionNickname.clear();
            QMessageBox::warning(&profileEdit, QStringLiteral("设置密码失败"), message);
            return;
        }
        passwordChange.setSubmitting(false, message);
        if (!error.resultUnknown && error.code == QStringLiteral("AUTH_FAIL")) {
            pendingOldPassword.clear();
            passwordChange.setStep(PasswordChangeStep::VerifyOriginal, message);
        }
    });
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
                     &app, [&](const QString &stationId) { openStationDetails(stationId); });
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
                     &app, [&](const QString &stationId, const QString &chargerId) {
        clearExpiredReservation();
        if (activeReservation) {
            openStationDetails(activeReservation->stationId);
            return;
        }
        confirmationOpenedFromScanner = false;
        chargeBinder.chargeConfirmationRequested(stationId, chargerId);
    });
    // 兼容详情页旧版意图信号：正式入口统一转入当前选桩链路。
    QObject::connect(&stationDetail, &StationDetailWindow::navigationRequested,
                     &app, [&] {
        mapBinder.routePreviewRequested(TravelMode::Driving);
    });
    QObject::connect(&stationDetail, &StationDetailWindow::chargeRequested,
                     &app, [&] {
        const StationDetailViewState state = mapBinder.currentStationDetailState();
        clearExpiredReservation();
        if (activeReservation) {
            openStationDetails(activeReservation->stationId);
            return;
        }
        if (!state.stationId.isEmpty() && !state.selectedChargerId.isEmpty()) {
            confirmationOpenedFromScanner = false;
            chargeBinder.chargeConfirmationRequested(state.stationId,
                                                     state.selectedChargerId);
        }
    });
    QObject::connect(&chargeBinder, &IChargingUiBinder::confirmationStateChanged,
                     &chargeConfirmation, &ChargeConfirmationWindow::render);
    QObject::connect(&chargeBinder, &IChargingUiBinder::confirmationPageRequested,
                     &app, [&] {
        chargeConfirmation.render(chargeBinder.currentState());
        mainWindow.renderSecondaryPage(&chargeConfirmation);
    });
    QObject::connect(&chargeBinder, &IChargingUiBinder::stationDetailPageRequested,
                     &app, [&] {
        if (confirmationOpenedFromScanner) {
            confirmationOpenedFromScanner = false;
            mainWindow.renderPrimaryPage(MainWindow::PrimaryPage::Charging);
        } else {
            stationDetail.render(mapBinder.currentStationDetailState());
            mainWindow.renderSecondaryPage(&stationDetail);
        }
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
        orderListOpen = true;
        orderList.resetFilter();
        orderListBaseState = OrderListViewState{
            {}, QStringLiteral("正在加载订单、预约和钱包流水…")};
        orderList.render(orderListBaseState);
        mainWindow.renderSecondaryPage(&orderList);
        orderListRequestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        orderService.queryOrderHistory({orderListRequestId, {}});
        reservationHistory.clear();
        walletBinder.activate();
        reservationService.queryHistory(
            {QUuid::createUuid().toString(QUuid::WithoutBraces), {}});
    });
    const auto showProfileNotice = [&](const QString &title, const QString &text) {
        QMessageBox::information(&mainWindow, title, text);
    };
    QObject::connect(&mainWindow, &MainWindow::helpFeedbackPageRequested,
                     &app, [&] {
        profileText.renderContent(
            QStringLiteral("帮助与反馈"),
            QStringLiteral("如果您在站点查询、路线导航、预约、扫码充电、订单支付或钱包使用过程中遇到问题，请记录发生时间、站点名称、充电桩编号和页面提示。\n\n当前服务端尚未提供反馈提交协议，因此本页暂不上传内容；联调问题可将上述信息交给项目组排查。"));
        mainWindow.renderSecondaryPage(&profileText);
    });
    QObject::connect(&mainWindow, &MainWindow::aboutPageRequested,
                     &app, [&] {
        profileText.renderContent(
            QStringLiteral("关于智充"),
            QStringLiteral("智充实训版 1.0\n\n提供充电站查询、腾讯地图导航、充电桩预约、扫码充电、实时充电状态、订单结算和钱包服务。业务结果以服务端返回的数据为准。"));
        mainWindow.renderSecondaryPage(&profileText);
    });
    QObject::connect(&profileText, &ProfileTextWindow::backRequested,
                     &app, [&] {
        mainWindow.renderPrimaryPage(MainWindow::PrimaryPage::Profile);
    });
    QObject::connect(&stationDetail, &StationDetailWindow::chargerSelected,
                     &mapBinder, &IMapUiBinder::chargerSelected);
    // 预约状态变化由服务端/预约 Binder 决定；详情页事件必须有明确反馈，不能静默无响应。
    QObject::connect(&stationDetail,
                     &StationDetailWindow::reservationExpiredRefreshRequested,
                     &app, [&] {
        mapBinder.stationRefreshRequested();
    });
    QObject::connect(&stationDetail,
                     &StationDetailWindow::cancelReservationRequested,
                     &reservationBinder,
                     &ReservationUiBinder::cancelReservationRequested);
    QObject::connect(&stationDetail,
                     &StationDetailWindow::cancelReservationRetryRequested,
                     &reservationBinder,
                     &ReservationUiBinder::cancelReservationRetryRequested);
    QObject::connect(&stationDetail,
                     &StationDetailWindow::activeReservationRequested,
                     &app, [&](const QString &, const QString &stationId, const QString &) {
        if (!stationId.trimmed().isEmpty())
            openStationDetails(stationId);
    });
    const auto appendRechargeOrders = [&](OrderListViewState state) {
        const WalletViewState wallet = walletBinder.currentState();
        QSet<QString> seen;
        for (const WalletTransaction &transaction : wallet.recentTransactions) {
            const QString fallbackKey = QStringLiteral("%1|%2|%3")
                                            .arg(transaction.orderId)
                                            .arg(transaction.createdAtUtc.toSecsSinceEpoch())
                                            .arg(transaction.amountCents);
            const QString key = transaction.transactionId.isEmpty()
                                    ? fallbackKey : transaction.transactionId;
            if (seen.contains(key)) continue;
            seen.insert(key);
            OrderListItemView item;
            item.businessId = key;
            item.type = OrderBusinessType::Recharge;
            item.stationName = QStringLiteral("钱包账户");
            item.createdAtText = transaction.createdAtUtc.isValid()
                ? transaction.createdAtUtc.toLocalTime().toString(Qt::ISODate)
                : QStringLiteral("时间未知");
            const bool outgoing = transaction.type == WalletTransactionType::Payment
                                  || transaction.type == WalletTransactionType::Deposit;
            item.amountText = QStringLiteral("%1¥%2")
                                  .arg(outgoing ? QStringLiteral("-") : QStringLiteral("+"))
                                  .arg(qAbs(transaction.amountCents) / 100.0, 0, 'f', 2);
            QString kind = QStringLiteral("钱包流水");
            if (transaction.type == WalletTransactionType::Recharge)
                kind = QStringLiteral("钱包充值");
            else if (transaction.type == WalletTransactionType::Payment)
                kind = QStringLiteral("订单支付");
            else if (transaction.type == WalletTransactionType::Refund)
                kind = QStringLiteral("预约/订单退款");
            else if (transaction.type == WalletTransactionType::Deposit)
                kind = QStringLiteral("预约押金");
            item.statusText = QStringLiteral("已完成");
            item.statusTone = QStringLiteral("success");
            item.summaryText = transaction.orderId.isEmpty()
                ? kind
                : QStringLiteral("%1 · 订单 %2").arg(kind, transaction.orderId);
            item.action = OrderListAction::None;
            state.orders.append(item);
        }
        for (const ReservationHistoryItem &reservation : reservationHistory) {
            OrderListItemView item;
            item.businessId = reservation.reservationId;
            item.type = OrderBusinessType::Reservation;
            item.stationName = reservation.stationName;
            item.chargerCode = reservation.chargerCode;
            const QDateTime time = reservation.reserveAtUtc.isValid() ? reservation.reserveAtUtc : reservation.createdAtUtc;
            item.createdAtText = time.isValid() ? time.toLocalTime().toString(Qt::ISODate) : QStringLiteral("时间未知");
            const qint64 depositCents = reservation.depositCents > 0
                                            ? reservation.depositCents : 2000;
            item.amountText = QStringLiteral("¥%1").arg(depositCents / 100.0, 0, 'f', 2);
            item.statusText = reservation.status.isEmpty() ? QStringLiteral("预约记录") : reservation.status;
            item.statusTone = QStringLiteral("neutral");
            item.summaryText = QStringLiteral("预约充电桩 %1").arg(item.chargerCode);
            item.action = OrderListAction::ViewDetails;
            item.actionText = QStringLiteral("查看详情");
            state.orders.append(item);
        }
        std::sort(state.orders.begin(), state.orders.end(),
                  [](const OrderListItemView &left, const OrderListItemView &right) {
            return left.createdAtText > right.createdAtText;
        });
        return state;
    };
    const auto orderItemDetailState = [](const OrderListItemView &order) {
        OrderDetailViewState detail;
        detail.businessId = order.businessId;
        detail.relatedBusinessId = order.relatedBusinessId;
        detail.stationId = order.stationId;
        detail.chargerId = order.chargerId;
        detail.type = order.type;
        detail.titleText = order.type == OrderBusinessType::Charging
                               ? QStringLiteral("充电订单")
                           : order.type == OrderBusinessType::Reservation
                               ? QStringLiteral("预约订单")
                               : QStringLiteral("钱包充值");
        detail.stationName = order.stationName;
        detail.chargerCode = order.chargerCode;
        detail.createdAtText = order.createdAtText;
        detail.durationText = order.durationText;
        detail.energyText = order.energyText;
        detail.amountText = order.amountText;
        detail.paymentMethodText = QStringLiteral("钱包支付");
        detail.statusText = order.statusText;
        detail.statusTone = order.statusTone;
        detail.message = order.summaryText;
        if (order.action != OrderListAction::ViewDetails) {
            detail.action = order.action;
            detail.actionText = order.actionText;
            detail.actionEnabled = order.action != OrderListAction::None;
        }
        return detail;
    };
    const auto currentOrderItem = [&](const QString &businessId,
                                      OrderBusinessType type)
        -> std::optional<OrderListItemView> {
        const OrderListViewState current = appendRechargeOrders(orderListBaseState);
        for (const OrderListItemView &order : current.orders) {
            if (order.businessId == businessId && order.type == type) return order;
        }
        return std::nullopt;
    };
    const auto renderFrequentStations = [&] {
        QHash<QString, FrequentStationItemView> aggregated;
        for (const OrderListItemView &order : orderListBaseState.orders) {
            if (order.type == OrderBusinessType::Recharge) continue;
            const QString key = order.stationId.isEmpty()
                                    ? order.stationName : order.stationId;
            if (key.isEmpty() || order.stationName.isEmpty()) continue;
            FrequentStationItemView item = aggregated.value(key);
            item.stationId = order.stationId;
            item.stationName = order.stationName;
            ++item.orderCount;
            if (item.lastUsedText.isEmpty()) item.lastUsedText = order.createdAtText;
            aggregated.insert(key, item);
        }
        FrequentStationsViewState state;
        state.stations = aggregated.values();
        std::sort(state.stations.begin(), state.stations.end(),
                  [](const FrequentStationItemView &left,
                     const FrequentStationItemView &right) {
            if (left.orderCount != right.orderCount)
                return left.orderCount > right.orderCount;
            return left.stationName < right.stationName;
        });
        if (state.stations.isEmpty()) {
            state.message = QStringLiteral(
                "暂无可统计的历史订单，完成充电后常用站点会显示在这里。");
        }
        frequentStations.render(state);
    };
    const auto renderChargingOrders = [&](const QVector<ChargingOrder> &orders) {
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
            item.durationText = orderDurationText(order);
            item.statusText = orderStatusText(order.status);
            item.statusTone = orderStatusTone(order.status);
            item.summaryText = QStringLiteral("电量 %1").arg(item.energyText);
            item.action = order.status == OrderStatus::Charging ? OrderListAction::ViewCharging
                          : order.status == OrderStatus::PendingSettlement
                          ? OrderListAction::ContinuePayment : OrderListAction::ViewDetails;
            item.actionText = order.status == OrderStatus::Charging ? QStringLiteral("查看")
                            : order.status == OrderStatus::PendingSettlement
                            ? QStringLiteral("去结算") : QStringLiteral("查看详情");
            state.orders.append(item);
        }
        orderListBaseState = state;
        state = appendRechargeOrders(state);
        state.message = state.orders.isEmpty() ? QStringLiteral("暂无订单") : QString();
        if (orderListOpen) orderList.render(state);
        if (frequentStationsOpen) renderFrequentStations();
    };
    QObject::connect(&reservationService, &IReservationService::reservationHistoryReady,
                     &app, [&](const RequestContext &, const QVector<ReservationHistoryItem> &items) {
        reservationHistory = items;
        // 本地缓存可能因换机器/清理配置而缺失；登录后以服务端 reservation
        // 表中的 Active 记录校准预约状态，确保扫码/启动错误提示使用真实预约。
        if (!activeReservation) {
            for (const ReservationHistoryItem &item : items) {
                if (item.status.compare(QStringLiteral("Active"), Qt::CaseInsensitive) != 0
                    || item.chargerCode.trimmed().isEmpty())
                    continue;
                ActiveReservationView restored;
                restored.reservationId = item.reservationId;
                restored.stationId = item.stationName;
                restored.chargerId = item.chargerCode;
                restored.canCancel = true;
                restored.remainingText = QStringLiteral("预约已恢复");
                activeReservation = restored;
                reservationBinder.restoreActiveReservation(activeReservation);
                break;
            }
        }
        if (orderListOpen) orderList.render(appendRechargeOrders(orderListBaseState));
    });
    QObject::connect(&orderService, &IOrderService::activeOrdersReady,
                     &app, [&](const RequestContext &, const QVector<ChargingOrder> &orders) {
        renderChargingOrders(orders);
    });
    QObject::connect(&orderService, &IOrderService::orderHistoryReady,
                     &app, [&](const RequestContext &context,
                               const QVector<ChargingOrder> &orders) {
        if (context.requestId == frequentStationsRequestId)
            frequentStationsRequestId.clear();
        if (context.requestId == orderListRequestId)
            orderListRequestId.clear();
        renderChargingOrders(orders);
    });
    QObject::connect(&mainWindow, &MainWindow::commonStationsPageRequested,
                     &app, [&] {
        frequentStationsOpen = true;
        orderListOpen = false;
        frequentStations.render(
            FrequentStationsViewState{{}, QStringLiteral("正在加载常用充电站…")});
        mainWindow.renderSecondaryPage(&frequentStations);
        frequentStationsRequestId =
            QUuid::createUuid().toString(QUuid::WithoutBraces);
        orderService.queryOrderHistory({frequentStationsRequestId, {}});
    });
    QObject::connect(&frequentStations, &FrequentStationsWindow::backRequested,
                     &app, [&] {
        frequentStationsOpen = false;
        if (!frequentStationsRequestId.isEmpty())
            orderService.cancel(frequentStationsRequestId);
        frequentStationsRequestId.clear();
        mainWindow.renderPrimaryPage(MainWindow::PrimaryPage::Profile);
    });
    QObject::connect(&frequentStations, &FrequentStationsWindow::stationRequested,
                     &app, [&](const QString &stationId) {
        frequentStationsOpen = false;
        openStationDetails(stationId);
    });
    QObject::connect(&orderList, &OrderListWindow::backRequested,
                     &app, [&] {
        orderListOpen = false;
        if (!orderListRequestId.isEmpty()) orderService.cancel(orderListRequestId);
        orderListRequestId.clear();
        mainWindow.renderPrimaryPage(MainWindow::PrimaryPage::Profile);
    });
    QObject::connect(&walletBinder, &WalletUiBinder::stateChanged,
                     &app, [&](const WalletViewState &wallet) {
        if (!orderListOpen) return;
        Q_UNUSED(wallet);
        orderList.render(appendRechargeOrders(orderListBaseState));
    });
    QObject::connect(&orderList, &OrderListWindow::refreshRequested,
                     &app, [&] {
        if (!orderListRequestId.isEmpty()) return;
        orderListBaseState.message = QStringLiteral("正在刷新订单、预约和钱包流水…");
        orderList.render(appendRechargeOrders(orderListBaseState));
        orderListRequestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        orderService.queryOrderHistory({orderListRequestId, {}});
        reservationHistory.clear();
        walletBinder.activate();
        reservationService.queryHistory(
            {QUuid::createUuid().toString(QUuid::WithoutBraces), {}});
    });
    const auto handleOrderAction = [&](const QString &orderId,
                                       OrderBusinessType type,
                                       OrderListAction action) {
        if (orderId.trimmed().isEmpty()) {
            showProfileNotice(QStringLiteral("订单"),
                              QStringLiteral("订单编号缺失，无法继续操作。"));
            return;
        }
        const std::optional<OrderListItemView> cached = currentOrderItem(orderId, type);
        if (action == OrderListAction::ViewCharging) {
            const QString activeOrderId = cached && !cached->relatedBusinessId.isEmpty()
                                              ? cached->relatedBusinessId : orderId;
            sessionBinder.sessionRequested(activeOrderId);
            mainWindow.renderSecondaryPage(&sessionWindow);
            return;
        }
        if (type != OrderBusinessType::Charging) {
            if (!cached) {
                showProfileNotice(QStringLiteral("订单"),
                                  QStringLiteral("当前订单摘要已经失效，请刷新订单列表。"));
                return;
            }
            OrderDetailViewState detail = orderItemDetailState(*cached);
            if (action == OrderListAction::StartReservedCharging) {
                detail.message = QStringLiteral(
                    "预约记录已展示；正式服务端尚未提供预约历史到扫码上下文的查询合同。请从站点详情进入扫码充电。");
                detail.action = OrderListAction::None;
                detail.actionEnabled = false;
            }
            orderDetail.render(detail);
            mainWindow.renderSecondaryPage(&orderDetail);
            return;
        }

        pendingOrderDetailRequestId =
            QUuid::createUuid().toString(QUuid::WithoutBraces);
        pendingOrderDetailOrderId = orderId;
        pendingOrderDetailDestination = action == OrderListAction::ContinuePayment
                                            ? OrderDetailDestination::Settlement
                                            : OrderDetailDestination::Detail;
        settlementOpenedFromOrderList =
            pendingOrderDetailDestination == OrderDetailDestination::Settlement;
        if (pendingOrderDetailDestination == OrderDetailDestination::Detail) {
            OrderDetailViewState loading = cached
                                               ? orderItemDetailState(*cached)
                                               : OrderDetailViewState{};
            loading.businessId = orderId;
            loading.type = OrderBusinessType::Charging;
            loading.titleText = QStringLiteral("充电订单");
            loading.message = QStringLiteral("正在加载服务端订单详情…");
            loading.action = OrderListAction::None;
            loading.actionEnabled = false;
            orderDetail.render(loading);
            mainWindow.renderSecondaryPage(&orderDetail);
        }
        orderService.queryOrderDetail({pendingOrderDetailRequestId, {}}, orderId);
    };
    QObject::connect(&orderList, &OrderListWindow::orderActionRequested,
                     &app, handleOrderAction);
    QObject::connect(&orderDetail, &OrderDetailWindow::actionRequested,
                     &app, handleOrderAction);
    QObject::connect(&orderDetail, &OrderDetailWindow::backRequested,
                     &app, [&] {
        if (!pendingOrderDetailRequestId.isEmpty())
            orderService.cancel(pendingOrderDetailRequestId);
        pendingOrderDetailRequestId.clear();
        pendingOrderDetailOrderId.clear();
        pendingOrderDetailDestination = OrderDetailDestination::None;
        orderList.render(appendRechargeOrders(orderListBaseState));
        mainWindow.renderSecondaryPage(&orderList);
    });
    QObject::connect(&walletRecharge, &WalletRechargeWindow::backRequested,
                     &app, [&] {
        if (walletEntryPoint == WalletEntryPoint::ChargeConfirmation) {
            mainWindow.renderSecondaryPage(&chargeConfirmation);
        } else if (walletEntryPoint == WalletEntryPoint::Payment) {
            paymentOpen = true;
            renderPayment();
            mainWindow.renderSecondaryPage(&paymentWindow);
        } else {
            mainWindow.renderPrimaryPage(MainWindow::PrimaryPage::Profile);
        }
    });
    // 阶段 E：钱包页面使用服务端余额与流水；充值结果未知时由 Binder 锁定重试。
    QObject::connect(&walletBinder, &WalletUiBinder::stateChanged,
                     &walletRecharge, &WalletRechargeWindow::render);
    QObject::connect(&walletBinder, &WalletUiBinder::stateChanged,
                     &app, [&](const WalletViewState &) {
        if (paymentOpen) renderPayment();
    });
    QObject::connect(&walletRecharge, &WalletRechargeWindow::rechargeRequested,
                     &walletBinder, &WalletUiBinder::rechargeRequested);
    QObject::connect(&walletBinder, &WalletUiBinder::profileRefreshRequested,
                     userService, &IUserService::refreshCurrentUser);

    // 合同 §3.1：会话页双向接线（意图 → Binder，状态 → 渲染）。
    QObject::connect(&sessionBinder,
                     &IChargingSessionUiBinder::sessionStateChanged,
                     &sessionWindow, &ChargingSessionWindow::render);
    QObject::connect(&sessionBinder,
                     &IChargingSessionUiBinder::sessionStateChanged,
                     &mainWindow, &MainWindow::renderChargingSession);
    QObject::connect(&sessionBinder,
                     &IChargingSessionUiBinder::activeSessionsStateChanged,
                     &sessionWindow, &ChargingSessionWindow::renderSessions);
    QObject::connect(&sessionBinder,
                     &IChargingSessionUiBinder::activeSessionsStateChanged,
                     &mainWindow, &MainWindow::renderChargingSessions);
    QObject::connect(&sessionWindow, &ChargingSessionWindow::refreshRequested,
                     &sessionBinder, &IChargingSessionUiBinder::refreshRequested);
    QObject::connect(&sessionWindow, &ChargingSessionWindow::backRequested,
                     &app, [&] {
        mainWindow.renderPrimaryPage(MainWindow::PrimaryPage::Home);
    });
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
    // 主窗口内嵌会话页与二级会话页共享同一 Binder，保证两个入口按钮行为一致。
    QObject::connect(&mainWindow, &MainWindow::chargingRefreshRequested,
                     &sessionBinder, &IChargingSessionUiBinder::refreshRequested);
    QObject::connect(&mainWindow, &MainWindow::stopChargingRequested,
                     &sessionBinder, &IChargingSessionUiBinder::stopChargingRequested);
    QObject::connect(&mainWindow, &MainWindow::recoverStopResultRequested,
                     &sessionBinder, &IChargingSessionUiBinder::recoverStopResultRequested);
    QObject::connect(&mainWindow, &MainWindow::activeSessionsRequested,
                     &sessionBinder, &IChargingSessionUiBinder::activeSessionsRequested);
    QObject::connect(&mainWindow, &MainWindow::activeSessionSelected,
                     &sessionBinder, &IChargingSessionUiBinder::activeSessionSelected);
    const auto openScanner = [&](ScanEntryPoint entryPoint) {
        clearExpiredReservation();
        if (activeReservation) {
            openStationDetails(activeReservation->stationId);
            return;
        }
        scanEntryPoint = entryPoint;
        ScanViewState scanState;
        scanState.status = qrScanner.cameraAvailable() ? ScanStatus::RequestingPermission : ScanStatus::Error;
        scanState.message = qrScanner.cameraAvailable()
            ? QStringLiteral("点击允许摄像头后开始实时扫码")
            : QStringLiteral("当前设备没有可用摄像头，请从相册选择二维码。");
        scanState.cameraAvailable = qrScanner.cameraAvailable();
        scanState.cameraPermissionGranted = false;
        scanState.canImportImage = true;
        scanState.canRetry = false;
        qrScanner.render(scanState);
        mainWindow.renderSecondaryPage(&qrScanner);
    };
    QObject::connect(&mainWindow, &MainWindow::scanChargingRequested,
                     &app, [&] { openScanner(ScanEntryPoint::PrimaryCharging); });
    QObject::connect(&sessionWindow, &ChargingSessionWindow::scanChargingRequested,
                     &app, [&] { openScanner(ScanEntryPoint::Session); });
    const auto handleDetectedQr = [&](const QString &raw) {
        clearExpiredReservation();
        if (activeReservation) {
            openStationDetails(activeReservation->stationId);
            return;
        }
        ScanViewState state;
        state.canImportImage = true;
        const QString chargerCode = chargerCodeFromQr(raw);
        if (chargerCode.isEmpty()) {
            state.status = ScanStatus::Error;
            state.message = QStringLiteral("二维码内容不包含合法的 chargerCode。");
            state.canRetry = true;
            qrScanner.render(state);
            return;
        }
        state.status = ScanStatus::Validating;
        state.chargerDisplayText = chargerCode;
        state.message = QStringLiteral("二维码识别成功，正在加载充电确认信息…");
        state.canImportImage = false;
        qrScanner.render(state);
        confirmationOpenedFromScanner = true;
        chargeBinder.chargeConfirmationByChargerCodeRequested(chargerCode);
    };
    QObject::connect(&qrScanner, &QrCodeScannerWindow::qrCodeDetected,
                     &app, handleDetectedQr);
    QObject::connect(&qrScanner, &QrCodeScannerWindow::cameraPermissionRequested,
                     &app, [&] {
        ScanViewState state;
        state.status = qrScanner.cameraAvailable() ? ScanStatus::Scanning : ScanStatus::Error;
        state.cameraAvailable = qrScanner.cameraAvailable();
        state.cameraPermissionGranted = state.cameraAvailable;
        state.message = state.cameraAvailable ? QStringLiteral("对准二维码后将自动识别")
                                               : QStringLiteral("当前设备没有可用摄像头，请从相册选择二维码。");
        state.canImportImage = true;
        qrScanner.render(state);
    });
    QObject::connect(&qrScanner, &QrCodeScannerWindow::scanRetryRequested,
                     &app, [&] {
        ScanViewState state;
        state.cameraAvailable = qrScanner.cameraAvailable();
        state.cameraPermissionGranted = state.cameraAvailable;
        state.status = state.cameraAvailable ? ScanStatus::Scanning : ScanStatus::Error;
        state.message = state.cameraAvailable ? QStringLiteral("正在重新打开摄像头…")
                                               : QStringLiteral("当前没有可用摄像头扫码适配器。");
        state.canImportImage = true;
        qrScanner.render(state);
    });
    QObject::connect(&qrScanner, &QrCodeScannerWindow::imageImportRequested,
                     &app, [&] {
        const QString path = QFileDialog::getOpenFileName(
            &qrScanner, QStringLiteral("选择二维码图片"), QString(),
            QStringLiteral("图片 (*.png *.jpg *.jpeg *.bmp)"));
        if (path.isEmpty()) return;
        ScanViewState state;
        state.canImportImage = true;
        QString decodeError;
        const QString raw = decodeQrImage(path, &decodeError);
        if (raw.isEmpty()) {
            state.status = ScanStatus::Error;
            state.message = decodeError;
            state.canRetry = true;
            qrScanner.render(state);
            return;
        }
        handleDetectedQr(raw);
    });
    QObject::connect(&qrScanner, &QrCodeScannerWindow::torchToggleRequested,
                     &app, [&](bool) {
        ScanViewState state;
        state.status = ScanStatus::Error;
        state.message = QStringLiteral("当前设备不支持扫码手电筒控制。");
        state.canImportImage = true;
        qrScanner.render(state);
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
    QObject::connect(&settlementBinder, &SettlementUiBinder::stateChanged,
                     &app, [&](const SettlementViewState &) {
        if (paymentOpen) renderPayment();
    });
    QObject::connect(&settlementWindow, &SettlementWindow::paymentRequested,
                     &app, [&](const QString &) {
        paymentOpen = true;
        walletBinder.activate();
        renderPayment();
        mainWindow.renderSecondaryPage(&paymentWindow);
    });
    QObject::connect(&paymentWindow, &PaymentWindow::backRequested,
                     &app, [&] {
        if (reservationPaymentOpen) {
            reservationPaymentOpen = false;
            paymentOpen = false;
            reservationConfirmation.render(reservationBinder.currentState());
            mainWindow.renderSecondaryPage(&reservationConfirmation);
            return;
        }
        paymentOpen = false;
        settlementWindow.render(settlementBinder.currentState());
        mainWindow.renderSecondaryPage(&settlementWindow);
    });
    QObject::connect(&paymentWindow, &PaymentWindow::rechargeRequested,
                     &app, [&] { openWallet(WalletEntryPoint::Payment); });
    QObject::connect(&paymentWindow, &PaymentWindow::payRequested,
                     &app, [&](const QString &orderId, PaymentPurpose purpose) {
        if (purpose == PaymentPurpose::Reservation && reservationPaymentOpen) {
            Q_UNUSED(orderId);
            reservationBinder.setConfirmationState(pendingReservationPayment);
            reservationBinder.reserveRequested(
                pendingReservationPayment.stationId,
                pendingReservationPayment.chargerId,
                pendingReservationPayment.durationSeconds);
            return;
        }
        if (purpose != PaymentPurpose::ChargingSettlement
            || orderId != settlementBinder.currentState().orderId) {
            return;
        }
        settlementBinder.payRequested();
    });
    QObject::connect(&paymentWindow, &PaymentWindow::paymentResultRefreshRequested,
                     &app, [&](const QString &) {
        if (reservationPaymentOpen) {
            renderPayment();
            return;
        }
        settlementBinder.refreshRequested();
        renderPayment();
    });
    QObject::connect(&settlementWindow, &SettlementWindow::backRequested,
                     &app, [&] {
        if (settlementOpenedFromOrderList) {
            settlementOpenedFromOrderList = false;
            mainWindow.renderSecondaryPage(&orderList);
        } else {
            sessionWindow.render(sessionBinder.currentState());
            mainWindow.renderSecondaryPage(&sessionWindow);
        }
    });
    QObject::connect(&settlementBinder, &SettlementUiBinder::orderRefreshRequested,
                     &app, [&] {
        if (orderListOpen) {
            orderService.queryOrderHistory(
                {QUuid::createUuid().toString(QUuid::WithoutBraces), {}});
        }
    });
    QObject::connect(&settlementBinder, &SettlementUiBinder::walletRefreshRequested,
                     &walletBinder, &WalletUiBinder::activate);

    // 阶段 D：登录成功 → 注入身份并自动恢复活动订单（充电中/待结算）。
    QObject::connect(userService, &IUserService::loginSucceeded,
                     &app, [&](const LoginResult &result) {
#ifndef CHARGINGUSER_USER_DEMO
        orderService.setIdentity(result.session.profile.userId);
        walletNetwork.setIdentity(result.session.profile.userId);
        chargingNetwork.setIdentity(result.session.profile.userId);
        reservationService.setIdentity(result.session.profile.userId);
        restoreReservation(result.session.profile.userId);
        reservationService.queryHistory(
            {QUuid::createUuid().toString(QUuid::WithoutBraces), {}});
        pushDispatcher.setIdentity(result.session.profile.userId);
#endif
        walletBinder.setAccountId(result.session.profile.userId);
        walletBinder.activate();
        RequestContext recoveryContext{
            QUuid::createUuid().toString(QUuid::WithoutBraces), {}};
        orderService.queryActiveOrder(recoveryContext);
    });
    QObject::connect(userService, &IUserService::logoutSucceeded,
                     &app, [&](const OperationResult &) {
        clearReservation();
        activeUserId.clear();
#ifndef CHARGINGUSER_USER_DEMO
        orderService.setIdentity(QString());
        walletNetwork.setIdentity(QString());
        chargingNetwork.setIdentity(QString());
        reservationService.setIdentity(QString());
        pushDispatcher.setIdentity(QString());
#endif
    });
    // 活动订单存在时交由会话 Binder 拉取详情；待支付订单直达结算页（合同 §3.4）。
    QObject::connect(&orderService, &IOrderService::activeOrderReady,
                     &app, [&](const RequestContext &,
                               const std::optional<ChargingOrder> &active) {
        if (active.has_value()) {
            if (active->status == OrderStatus::Charging)
                mapBinder.chargerStatusConfirmed(active->stationId, active->chargerId,
                                                 ChargerBusinessStatus::Charging);
            // 214 已携带完整订单，直接渲染，避免旧服务端上重复详情查询
            // 因不回显 requestId 而长期停留在 Loading。
            sessionBinder.showOrder(*active);
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
        mapBinder.chargerStatusConfirmed(result.order.stationId,
                                         result.order.chargerId,
                                         ChargerBusinessStatus::Idle);
#ifdef CHARGINGUSER_USER_DEMO
        walletService.setOrderAmount(result.order.orderId, result.order.amountCents);
        chargingService.setChargerAvailable(result.order.stationId,
                                            result.order.chargerId, true);
#endif
        settlementBinder.showOrder(result.order);
        mainWindow.renderSecondaryPage(&settlementWindow);
    });
    QObject::connect(&orderService, &IOrderService::orderDetailReady,
                     &app, [&](const RequestContext &context,
                                const ChargingOrder &order) {
        if (context.requestId == pendingOrderDetailRequestId) {
            const OrderDetailDestination destination = pendingOrderDetailDestination;
            pendingOrderDetailRequestId.clear();
            pendingOrderDetailOrderId.clear();
            pendingOrderDetailDestination = OrderDetailDestination::None;
            if (destination == OrderDetailDestination::Detail) {
                orderDetail.render(orderDetailViewState(order));
                mainWindow.renderSecondaryPage(&orderDetail);
            } else {
                settlementBinder.showOrder(order);
                mainWindow.renderSecondaryPage(&settlementWindow);
            }
            return;
        }
        if (order.status == OrderStatus::Charging) {
            sessionBinder.showOrder(order);
            mainWindow.renderSecondaryPage(&sessionWindow);
        } else {
            settlementBinder.showOrder(order);
            mainWindow.renderSecondaryPage(&settlementWindow);
        }
    });
    QObject::connect(&orderService, &IOrderService::requestFailed,
                     &app, [&](const ClientError &error) {
        if (error.requestId == orderListRequestId) {
            orderListRequestId.clear();
            if (orderListOpen) {
                OrderListViewState state = appendRechargeOrders(orderListBaseState);
                state.message = error.displayMessage.isEmpty()
                                    ? QStringLiteral("订单加载失败，请点击刷新重试。")
                                    : error.displayMessage;
                orderList.render(state);
            }
            return;
        }
        if (error.requestId == frequentStationsRequestId) {
            frequentStationsRequestId.clear();
            if (frequentStationsOpen) {
                FrequentStationsViewState state;
                state.message = error.displayMessage.isEmpty()
                                    ? QStringLiteral("常用充电站加载失败，请稍后重试。")
                                    : error.displayMessage;
                frequentStations.render(state);
            }
            return;
        }
        if (error.requestId != pendingOrderDetailRequestId) return;
        const OrderDetailDestination destination = pendingOrderDetailDestination;
        const QString orderId = pendingOrderDetailOrderId;
        pendingOrderDetailRequestId.clear();
        pendingOrderDetailOrderId.clear();
        pendingOrderDetailDestination = OrderDetailDestination::None;
        if (destination == OrderDetailDestination::Detail) {
            OrderDetailViewState state;
            state.businessId = orderId;
            state.type = OrderBusinessType::Charging;
            state.titleText = QStringLiteral("充电订单");
            state.statusText = QStringLiteral("加载失败");
            state.statusTone = QStringLiteral("neutral");
            state.message = error.displayMessage.isEmpty()
                                ? QStringLiteral("订单详情加载失败，请返回后重试。")
                                : error.displayMessage;
            orderDetail.render(state);
            mainWindow.renderSecondaryPage(&orderDetail);
        } else {
            settlementOpenedFromOrderList = false;
            showProfileNotice(
                QStringLiteral("订单结算"),
                error.displayMessage.isEmpty()
                    ? QStringLiteral("订单详情加载失败，请刷新后重试。")
                    : error.displayMessage);
            mainWindow.renderSecondaryPage(&orderList);
        }
    });
#ifndef CHARGINGUSER_USER_DEMO
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
        sessionBinder.applyProgress(notice);
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
        const StationDetailViewState detail = mapBinder.currentStationDetailState();
        state.stationId = stationId;
        state.chargerId = chargerId;
        state.stationName = detail.name;
        state.stationAddress = detail.address;
        for (const ChargerListItemView &charger : detail.chargers) {
            if (charger.chargerId != chargerId) continue;
            state.chargerCode = charger.chargerId;
            state.chargerTypeText = charger.title;
            state.powerText = charger.powerText;
            break;
        }
        state.status = ReservationConfirmationStatus::Ready;
        state.canReserve = true;
        state.durationSeconds = 7200;
        state.durationText = QStringLiteral("2 小时");
        state.depositText = QStringLiteral("¥20.00");
        state.depositPolicyText = QStringLiteral("预约保持 2 小时，过期规则由服务端结算");
        reservationBinder.setConfirmationState(state);
        mainWindow.renderSecondaryPage(&reservationConfirmation);
    });
    QObject::connect(&reservationConfirmation,
                     &ReservationConfirmationWindow::reserveRequested,
                     &app, [&](const QString &stationId, const QString &chargerId,
                               int durationSeconds) {
        pendingReservationPayment = reservationBinder.currentState();
        pendingReservationPayment.stationId = stationId;
        pendingReservationPayment.chargerId = chargerId;
        pendingReservationPayment.durationSeconds = durationSeconds;
        reservationPaymentOpen = true;
        paymentOpen = true;
        walletBinder.activate();
        renderPayment();
        mainWindow.renderSecondaryPage(&paymentWindow);
    });
    QObject::connect(&reservationConfirmation,
                     &ReservationConfirmationWindow::reservationRefreshRequested,
                     &reservationBinder, &ReservationUiBinder::refreshRequested);
    QObject::connect(&reservationService, &IReservationService::reservationCreated,
                     &app, [&](const RequestContext &, const ReservationResult &) {
        reservationPaymentOpen = false;
        paymentOpen = false;
        mainWindow.renderPrimaryPage(MainWindow::PrimaryPage::Home);
    });
    QObject::connect(&reservationService, &IReservationService::reservationCancelled,
                     &app, [&](const RequestContext &, const ReservationCancellationResult &) {
        mapBinder.stationRefreshRequested();
        walletBinder.activate();
        mainWindow.renderPrimaryPage(MainWindow::PrimaryPage::Home);
    });
#ifndef CHARGINGUSER_USER_DEMO
    QObject::connect(&pushDispatcher, &ServerPushDispatcher::reservationExpired,
                     &app, [&](const QString &reservationId, const QString &) {
        if (activeReservation && (reservationId.isEmpty()
                                   || reservationId == activeReservation->reservationId)) {
            const ActiveReservationView expired = *activeReservation;
            clearReservation();
            mapBinder.chargerStatusConfirmed(expired.stationId, expired.chargerId,
                                             ChargerBusinessStatus::Idle);
            chargeBinder.setReservationActive(false);
            QMessageBox::information(&mainWindow, QStringLiteral("预约已过期"),
                                     QStringLiteral("您的预约已失效，可以重新选择充电桩。"));
        }
    });
#endif
#endif
    QObject::connect(&chargingService, &IChargingService::chargingStarted,
                     &app, [&](const RequestContext &, const StartChargingResult &result) {
        mapBinder.chargerStatusConfirmed(result.stationId, result.chargerId,
                                         ChargerBusinessStatus::Charging);
#ifdef CHARGINGUSER_USER_DEMO
        ChargingOrder order;
        order.orderId = result.orderId;
        order.stationId = result.stationId;
        order.chargerId = result.chargerId;
        const ChargeConfirmationViewState confirmation = chargeBinder.currentState();
        order.stationName = confirmation.stationName;
        order.chargerCode = confirmation.chargerCode;
        order.chargerType = confirmation.chargerTypeText;
        order.currentPowerKw = 42.6;
        order.ratedPowerKw = 120.0;
        order.progressPercent = 1;
        order.priceCentsPerKwhSnapshot = result.priceCentsPerKwhSnapshot;
        order.startedAtUtc = result.startedAtUtc;
        order.status = OrderStatus::Charging;
        orderService.upsertOrder(order);
#endif
    });
    QObject::connect(&reservationBinder, &ReservationUiBinder::stateChanged,
                     &reservationConfirmation, &ReservationConfirmationWindow::render);
    QObject::connect(&reservationBinder, &ReservationUiBinder::stateChanged,
                     &app, [&](const ReservationConfirmationViewState &) {
        if (reservationPaymentOpen) renderPayment();
    });
    QObject::connect(&reservationConfirmation,
                     &ReservationConfirmationWindow::backRequested,
                     &app, [&] {
        stationDetail.render(mapBinder.currentStationDetailState());
        mainWindow.renderSecondaryPage(&stationDetail);
    });
    QObject::connect(&qrScanner, &QrCodeScannerWindow::backRequested,
                     &app, [&] {
        if (scanEntryPoint == ScanEntryPoint::Session)
            mainWindow.renderSecondaryPage(&sessionWindow);
        else
            mainWindow.renderPrimaryPage(MainWindow::PrimaryPage::Charging);
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
#ifndef CHARGINGUSER_USER_DEMO
    QObject::connect(&backend, &BackendClient::connectionStateChanged,
                     &login, [](ConnectionState state) {
        qInfo().noquote()
            << QStringLiteral("Network state: %1").arg(connectionStateName(state));
                     });
    QObject::connect(&backend, &BackendClient::frameReceived,
                     &app, [](int msgType, const QJsonObject &payload) {
        // 联调日志：仅输出充电/订单/钱包关键字段，禁止输出完整载荷和敏感凭据。
        switch (msgType) {
        case 208:
            qInfo().noquote() << QStringLiteral("Server 208 start: order=%1 charger=%2")
                                     .arg(payload.value(QStringLiteral("orderNo")).toString(),
                                          payload.value(QStringLiteral("chargerCode")).toString());
            break;
        case 225:
            qInfo().noquote() << QStringLiteral("Server 225 progress: order=%1 kwh=%2 amountCents=%3")
                                     .arg(payload.value(QStringLiteral("orderNo")).toString())
                                     .arg(payload.value(QStringLiteral("kwh")).toVariant().toString())
                                     .arg(payload.value(QStringLiteral("amountCents")).toVariant().toString());
            break;
        case 209:
            qInfo().noquote() << QStringLiteral("Server 209 stop: order=%1 kwh=%2 amountCents=%3")
                                     .arg(payload.value(QStringLiteral("orderNo")).toString())
                                     .arg(payload.value(QStringLiteral("kwh")).toVariant().toString())
                                     .arg(payload.value(QStringLiteral("amountCents")).toVariant().toString());
            break;
        case 214:
        {
            QJsonArray orders = payload.value(QStringLiteral("orders")).toArray();
            if (orders.isEmpty())
                orders = payload.value(QStringLiteral("data")).toArray();
            qInfo().noquote() << QStringLiteral("Server 214 orders: count=%1").arg(orders.size());
            for (const QJsonValue &value : orders) {
                if (!value.isObject()) continue;
                const QJsonObject order = value.toObject();
                qInfo().noquote()
                    << QStringLiteral("  order=%1 status=%2 charger=%3 kwh=%4 amountCents=%5")
                           .arg(order.value(QStringLiteral("orderNo")).toString(),
                                order.value(QStringLiteral("status")).toString(),
                                order.value(QStringLiteral("chargerCode")).toString(),
                                order.value(QStringLiteral("kwh")).toVariant().toString(),
                                order.value(QStringLiteral("amountCents")).toVariant().toString());
            }
            break;
        }
        case 200:
            qInfo().noquote() << QStringLiteral("Server 200 data: table=%1 count=%2")
                                     .arg(payload.value(QStringLiteral("table")).toString())
                                     .arg(payload.value(QStringLiteral("data")).toArray().size());
            break;
        case 215:
        case 216:
            qInfo().noquote() << QStringLiteral("Server %1 wallet result: balanceCents=%2")
                                     .arg(msgType)
                                     .arg(payload.value(QStringLiteral("balanceCents")).toVariant().toString());
            break;
        case 228:
            qInfo().noquote()
                << QStringLiteral("Server 228 profile update: ok=%1 usernamePresent=%2 changed=%3")
                       .arg(payload.value(QStringLiteral("ok")).toBool())
                       .arg(!payload.value(QStringLiteral("username")).toString().isEmpty())
                       .arg(payload.value(QStringLiteral("changed")).toArray().size());
            break;
        default:
            if (msgType >= 300 && msgType < 400) {
                QString message = payload.value(QStringLiteral("err")).toString();
                if (message.isEmpty()) message = payload.value(QStringLiteral("reason")).toString();
                qWarning().noquote()
                    << QStringLiteral("Server %1 error: code=%2 message=%3")
                           .arg(msgType)
                           .arg(payload.value(QStringLiteral("code")).toString(), message);
            }
            break;
        }
    });
    QObject::connect(&backend, &BackendClient::networkError,
                     &login, [](const QString &message) {
        qWarning().noquote() << QStringLiteral("Network error: %1").arg(message);
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     &backend, &BackendClient::shutdown);
#endif

    login.render(binder->currentLoginViewState());
    profileEdit.render(binder->currentProfileEditViewState());
    mainWindow.renderProfile(binder->currentProfileViewState());
    mainWindow.renderHome(mapBinder.currentHomeState());
    walletRecharge.render(walletBinder.currentState());
#ifdef CHARGINGUSER_USER_DEMO
    mapBinder.mapReady();
#else
    if (mapKey.isEmpty()) {
        mapBinder.mapLoadFailed();
    }
#endif
    showOnly(&login);

#ifdef CHARGINGUSER_USER_DEMO
    qInfo().noquote() << QStringLiteral("Starting shared UI flow with mock data adapters.");
#else
    qInfo().noquote()
        << QStringLiteral("Starting real-network entry for %1:%2.")
               .arg(host)
               .arg(portValue);
    if (trainingOperationsEnabled) {
        qWarning().noquote()
            << QStringLiteral("TEST_ONLY: training environment enabled by default; money/charging operations are allowed.");
    }
    backend.start();
#endif
    return app.exec();
}
