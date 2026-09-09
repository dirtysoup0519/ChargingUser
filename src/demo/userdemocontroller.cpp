#include "demo/userdemocontroller.h"
#include "demo/chargedemofixtureloader.h"
#include "demo/reservationdemofixtureloader.h"
#include "demo/mapdemofixtureloader.h"

#include "app/iuseruibinder.h"
#include "app/imapuibinder.h"
#include "app/reservationuibinder.h"
#include "modules/user/mockusernetworkapi.h"
#include "presentation/pages/auth/loginwindow.h"
#include "presentation/pages/shell/mainwindow.h"
#include "presentation/pages/profile/profileeditwindow.h"
#include "presentation/widgets/common/avatarimagehelper.h"
#include "presentation/pages/home/navigationwindow.h"
#include "presentation/pages/home/stationdetailwindow.h"
#include "presentation/pages/profile/walletrechargewindow.h"
#include "presentation/pages/charging/chargeconfirmationwindow.h"
#include "presentation/pages/charging/chargingsessionwindow.h"
#include "presentation/pages/charging/reservationconfirmationwindow.h"
#include "presentation/pages/charging/qrcodescannerwindow.h"
#include "presentation/pages/charging/settlementwindow.h"
#include "presentation/pages/charging/paymentwindow.h"
#include "presentation/pages/profile/orderlistwindow.h"
#include "presentation/pages/profile/orderdetailwindow.h"
#include "presentation/pages/profile/frequentstationswindow.h"
#include "presentation/pages/profile/profiletextwindow.h"
#include "presentation/pages/profile/passwordchangewindow.h"

#include <algorithm>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QTimer>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QFileDialog>
#include <QImage>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

#ifdef CHARGINGUSER_ENABLE_ZXING
#include <ZXing/BarcodeFormat.h>
#include <ZXing/ReadBarcode.h>
#ifdef CHARGINGUSER_ZXING_LEGACY
#include <ZXing/DecodeHints.h>
#else
#include <ZXing/ImageView.h>
#include <ZXing/ReaderOptions.h>
#endif
#endif

namespace
{

constexpr int DemoDelayMs = 450;

QString decodeQrImage(const QString &path, QString *error)
{
#ifdef CHARGINGUSER_ENABLE_ZXING
    QImage image(path);
    if (image.isNull()) { *error = QStringLiteral("无法读取所选图片。"); return {}; }
    image = image.convertToFormat(QImage::Format_Grayscale8);
    const ZXing::ImageView view(image.constBits(), image.width(), image.height(),
                                ZXing::ImageFormat::Lum, image.bytesPerLine());
#ifdef CHARGINGUSER_ZXING_LEGACY
    ZXing::DecodeHints options;
#else
    ZXing::ReaderOptions options;
#endif
    options.setFormats(ZXing::BarcodeFormat::QRCode);
    options.setTryHarder(true);
    const auto barcode = ZXing::ReadBarcode(view, options);
    if (!barcode.isValid()) { *error = QStringLiteral("图片中没有识别到有效二维码。"); return {}; }
#ifdef CHARGINGUSER_ZXING_LEGACY
    return QString::fromStdWString(barcode.text());
#else
    return QString::fromStdString(barcode.text());
#endif
#else
    Q_UNUSED(path)
    *error = QStringLiteral("当前 DEMO 构建未检测到 ZXing。");
    return {};
#endif
}

QString chargerCodeFromQr(const QString &raw)
{
    const QString value = raw.trimmed();
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(value.toUtf8(), &parseError);
    if (parseError.error == QJsonParseError::NoError && json.isObject())
        return json.object().value(QStringLiteral("chargerCode")).toString().trimmed();
    const QUrl url(value);
    const QString queryCode = QUrlQuery(url).queryItemValue(QStringLiteral("chargerCode"));
    if (!queryCode.isEmpty()) return queryCode.trimmed();
    static const QRegularExpression safeCode(QStringLiteral("^[A-Za-z0-9_.:-]{1,64}$"));
    return safeCode.match(value).hasMatch() ? value : QString();
}

qint64 moneyTextToCents(QString text)
{
    text.remove(QChar(0x00A5));
    text.remove(QStringLiteral("元"));
    text.remove(QLatin1Char(','));
    bool ok = false;
    const double amount = text.trimmed().toDouble(&ok);
    return ok ? qRound64(amount * 100.0) : -1;
}

QString balanceAfterPaymentText(qint64 balanceCents, const QString &amountText)
{
    const qint64 amountCents = moneyTextToCents(amountText);
    if (balanceCents < 0 || amountCents < 0)
        return QStringLiteral("--");
    if (balanceCents < amountCents)
        return QStringLiteral("余额不足");
    return QStringLiteral("¥%1").arg((balanceCents - amountCents) / 100.0,
                                     0, 'f', 2);
}

QHash<QString, DemoUserData> loadDemoUsers(QString *newUserNicknamePattern)
{
    QFile file(QStringLiteral(":/demo/user-demo-data.tmp"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (newUserNicknamePattern)
        *newUserNicknamePattern = root.value(QStringLiteral("defaultNewUserNickname"))
                                       .toString(QStringLiteral("用户{last4}"));
    QHash<QString, DemoUserData> users;
    for (const QJsonValue &value : root.value(QStringLiteral("users")).toArray()) {
        const QJsonObject object = value.toObject();
        DemoUserData user;
        user.nickname = object.value(QStringLiteral("nickname")).toString();
        user.password = object.value(QStringLiteral("password"))
                            .toString(QStringLiteral("123456"));
        user.avatarDataUri = object.value(QStringLiteral("avatar")).toString();
        const QJsonObject firstFailure =
            object.value(QStringLiteral("firstLoginFailure")).toObject();
        user.failFirstLogin = !firstFailure.isEmpty();
        user.firstLoginFailureCode = firstFailure.value(QStringLiteral("code")).toString();
        user.firstLoginFailureMessage = firstFailure.value(QStringLiteral("message")).toString();
        user.firstLoginFailureRetryable =
            firstFailure.value(QStringLiteral("retryable")).toBool();
        const QString status = object.value(QStringLiteral("status")).toString();
        if (status == QStringLiteral("frozen")) user.status = AccountStatus::Frozen;
        else if (status == QStringLiteral("unknown")) user.status = AccountStatus::Unknown;
        users.insert(object.value(QStringLiteral("phone")).toString(), user);
    }
    return users;
}

LoginResult makeLoginResult(const QString &phone, const DemoUserData *user,
                            const QString &newUserNicknamePattern)
{
    const bool isNewUser = user == nullptr;
    QString generatedNickname = newUserNicknamePattern;
    generatedNickname.replace(QStringLiteral("{last4}"), phone.right(4));
    LoginResult result;
    result.isNewUser = isNewUser;
    result.profileCompleted = !isNewUser;
    result.session.authenticated = true;
    result.session.accountStatus = user ? user->status : AccountStatus::Normal;
    result.session.profile.userId = QStringLiteral("U") + phone;
    result.session.profile.phone = phone;
    result.session.profile.nickname = user
        ? user->nickname
        : generatedNickname;
    result.session.profile.avatarKey = user ? user->avatarDataUri : QString();
    return result;
}

UserProfileResult makeProfileResult(const LoginResult &login)
{
    UserProfileResult result;
    result.profile = login.session.profile;
    result.accountStatus = login.session.accountStatus;
    return result;
}

} // namespace

UserDemoController::UserDemoController(MockUserNetworkApi *network,
                                       IUserUiBinder *binder,
                                       IMapUiBinder *mapBinder,
                                       ReservationUiBinder *reservationBinder,
                                       LoginWindow *login,
                                       ProfileEditWindow *profileEdit,
                                       MainWindow *mainWindow,
                                       QObject *parent)
    : QObject(parent)
    , m_network(network)
    , m_binder(binder)
    , m_mapBinder(mapBinder)
    , m_reservationBinder(reservationBinder)
    , m_login(login)
    , m_profileEdit(profileEdit)
    , m_mainWindow(mainWindow)
    , m_stationDetail(new StationDetailWindow(mainWindow))
    , m_navigation(new NavigationWindow(mainWindow))
    , m_walletRecharge(new WalletRechargeWindow(mainWindow))
    , m_chargeConfirmation(new ChargeConfirmationWindow(mainWindow))
    , m_reservationConfirmation(new ReservationConfirmationWindow(mainWindow))
    , m_qrScanner(new QrCodeScannerWindow(mainWindow))
    , m_settlement(new SettlementWindow(mainWindow))
    , m_payment(new PaymentWindow(mainWindow))
    , m_orderList(new OrderListWindow(mainWindow))
    , m_orderDetail(new OrderDetailWindow(mainWindow))
    , m_frequentStations(new FrequentStationsWindow(mainWindow))
    , m_profileText(new ProfileTextWindow(mainWindow))
    , m_passwordChange(new PasswordChangeWindow)
    , m_demoUsers(loadDemoUsers(&m_newUserNicknamePattern))
{
    Q_ASSERT(m_network);
    Q_ASSERT(m_binder);
    Q_ASSERT(m_mapBinder);
    Q_ASSERT(m_reservationBinder);
    Q_ASSERT(m_login);
    Q_ASSERT(m_profileEdit);
    Q_ASSERT(m_mainWindow);

    m_chargingSession = m_mainWindow->findChild<ChargingSessionWindow *>(
        QStringLiteral("chargingSessionWidget"));
    Q_ASSERT(m_chargingSession);

    // These widgets are constructed with MainWindow as their parent. Register
    // them before MainWindow is ever shown, otherwise Qt auto-shows ordinary
    // child widgets and the last-created recharge page covers the home page.
    m_mainWindow->registerSecondaryPage(m_stationDetail);
    m_mainWindow->registerSecondaryPage(m_navigation);
    m_mainWindow->registerSecondaryPage(m_walletRecharge);
    m_mainWindow->registerSecondaryPage(m_chargeConfirmation);
    m_mainWindow->registerSecondaryPage(m_reservationConfirmation);
    m_mainWindow->registerSecondaryPage(m_qrScanner);
    m_mainWindow->registerSecondaryPage(m_settlement);
    m_mainWindow->registerSecondaryPage(m_payment);
    m_mainWindow->registerSecondaryPage(m_orderList);
    m_mainWindow->registerSecondaryPage(m_orderDetail);
    m_mainWindow->registerSecondaryPage(m_frequentStations);
    m_mainWindow->registerSecondaryPage(m_profileText);

    QFile paymentFile(QStringLiteral(":/demo/payment-demo-data.tmp"));
    if (paymentFile.open(QIODevice::ReadOnly)) {
        const QJsonObject payment = QJsonDocument::fromJson(paymentFile.readAll()).object()
                                        .value(QStringLiteral("paymentDemo")).toObject();
        m_paymentBalanceText = payment.value(QStringLiteral("walletBalanceText")).toString(QStringLiteral("--"));
        m_paymentBalanceCents = moneyTextToCents(m_paymentBalanceText);
        m_paymentDelayMs = payment.value(QStringLiteral("paymentDelayMs")).toInt(450);
        m_paymentOutcome = payment.value(QStringLiteral("outcome")).toString(QStringLiteral("success"));
        const QJsonObject settlement = payment.value(QStringLiteral("settlement")).toObject();
        m_settlementState.payableText = settlement.value(QStringLiteral("payableText")).toString();
    }
    QFile orderListFile(QStringLiteral(":/demo/order-list-demo-data.tmp"));
    if (orderListFile.open(QIODevice::ReadOnly)) {
        const QJsonArray orders = QJsonDocument::fromJson(orderListFile.readAll()).object()
                                      .value(QStringLiteral("orders")).toArray();
        for (const QJsonValue &value : orders) {
            const QJsonObject object = value.toObject();
            OrderListItemView item;
            item.businessId = object.value(QStringLiteral("businessId")).toString();
            item.relatedBusinessId = object.value(QStringLiteral("relatedBusinessId")).toString();
            item.stationId = object.value(QStringLiteral("stationId")).toString();
            item.chargerId = object.value(QStringLiteral("chargerId")).toString();
            item.type = object.value(QStringLiteral("type")).toString() == QStringLiteral("reservation")
                            ? OrderBusinessType::Reservation : OrderBusinessType::Charging;
            item.stationName = object.value(QStringLiteral("stationName")).toString();
            item.chargerCode = object.value(QStringLiteral("chargerCode")).toString();
            item.createdAtText = object.value(QStringLiteral("createdAtText")).toString();
            item.summaryText = object.value(QStringLiteral("summaryText")).toString();
            item.durationText = object.value(QStringLiteral("durationText")).toString();
            item.energyText = object.value(QStringLiteral("energyText")).toString();
            item.amountText = object.value(QStringLiteral("amountText")).toString();
            item.statusText = object.value(QStringLiteral("statusText")).toString();
            item.statusTone = object.value(QStringLiteral("statusTone")).toString();
            item.actionText = object.value(QStringLiteral("actionText")).toString();
            const QString action = object.value(QStringLiteral("action")).toString();
            if (action == QStringLiteral("continue_payment")) item.action = OrderListAction::ContinuePayment;
            else if (action == QStringLiteral("view_charging")) item.action = OrderListAction::ViewCharging;
            else if (action == QStringLiteral("start_reserved_charging")) item.action = OrderListAction::StartReservedCharging;
            else item.action = OrderListAction::ViewDetails;
            m_orderListState.orders.append(item);
        }
    }

    QString chargeFixtureError;
    if (!loadChargeConfirmationDemo(QStringLiteral(":/demo/charge-demo-data.tmp"),
                                    &m_chargeConfirmationState,
                                    &chargeFixtureError)) {
        m_chargeConfirmationState.status = ChargeConfirmationStatus::Error;
        m_chargeConfirmationState.message = chargeFixtureError;
        m_chargeConfirmationState.canRetry = true;
    }

    QString chargingSessionFixtureError;
    if (loadChargingSessionDemo(QStringLiteral(":/demo/charging-session-demo-data.tmp"),
                                &m_chargingSessionDemoTemplates,
                                &chargingSessionFixtureError)) {
        // Fixture entries are templates created only after a successful scan/start.
        // Logging in must never fabricate an already-running charging order.
        renderChargingSessions();
    } else {
        ChargingSessionViewState errorState;
        errorState.status = ChargingSessionStatus::Error;
        errorState.message = chargingSessionFixtureError;
        errorState.canRefresh = true;
        m_chargingSession->render(errorState);
    }

    MapDemoFixture mapFixture;
    QString mapFixtureError;
    if (loadMapDemoFixture(QStringLiteral(":/demo/map-demo-data.tmp"),
                           &mapFixture, &mapFixtureError)) {
        m_demoStations = mapFixture.stations;
    }

    connect(m_chargingSession, &ChargingSessionWindow::activeSessionSelected,
            this, [this](const QString &orderId) {
        for (const ChargingSessionViewState &state : m_chargingSessionStates) {
            if (state.orderId != orderId) continue;
            renderChargingSessions(orderId);
            break;
        }
    });
    connect(m_chargingSession, &ChargingSessionWindow::stopChargingRequested,
            this, [this] {
        QString selectedId = m_selectedChargingOrderId;
        if (selectedId.isEmpty() && !m_chargingSessionStates.isEmpty())
            selectedId = m_chargingSessionStates.first().orderId;
        for (int index = 0; index < m_chargingSessionStates.size(); ++index) {
            const ChargingSessionViewState session = m_chargingSessionStates.at(index);
            if (session.orderId != selectedId) continue;
            m_settlementState.orderId = session.orderId;
            m_settlementState.stationName = session.stationName;
            m_settlementState.chargerCode = session.chargerCode;
            m_settlementState.durationText = session.durationText;
            m_settlementState.chargingTimeText = session.durationText;
            m_settlementState.energyText = session.energyText;
            m_settlementState.paymentMethodText = tr("钱包支付");
            m_settlementState.chargerInfoText = tr("%1 · %2号桩")
                                                    .arg(session.stationName,
                                                         session.chargerCode);
            m_settlementState.payableText = session.amountText;
            m_settlementState.canPay = true;
            m_settlementState.message.clear();
            OrderListItemView pendingOrder;
            pendingOrder.businessId = session.orderId;
            const QString sessionChargerKey =
                m_orderChargerKeys.value(session.orderId);
            pendingOrder.stationId = sessionChargerKey.section(
                QLatin1Char('\n'), 0, 0);
            pendingOrder.chargerId = sessionChargerKey.section(
                QLatin1Char('\n'), 1, 1);
            pendingOrder.type = OrderBusinessType::Charging;
            pendingOrder.stationName = session.stationName;
            pendingOrder.chargerCode = session.chargerCode;
            pendingOrder.createdAtText = tr("刚刚");
            pendingOrder.summaryText = tr("充电 %1 · %2").arg(session.durationText, session.energyText);
            pendingOrder.durationText = session.durationText;
            pendingOrder.energyText = session.energyText;
            pendingOrder.amountText = m_settlementState.payableText;
            pendingOrder.statusText = tr("待支付");
            pendingOrder.statusTone = QStringLiteral("warning");
            pendingOrder.actionText = tr("继续支付");
            pendingOrder.action = OrderListAction::ContinuePayment;
            for (int row = m_orderListState.orders.size() - 1; row >= 0; --row)
                if (m_orderListState.orders.at(row).businessId == session.orderId)
                    m_orderListState.orders.removeAt(row);
            m_orderListState.orders.prepend(pendingOrder);
            for (OrderListItemView &order : m_orderListState.orders) {
                if (order.type != OrderBusinessType::Reservation
                    || order.relatedBusinessId != session.orderId) continue;
                    order.statusText = tr("已使用");
                    order.statusTone = QStringLiteral("success");
                order.summaryText = tr("预约已使用 · 关联充电订单待结算");
                order.actionText = tr("查看详情");
                order.action = OrderListAction::ViewDetails;
                break;
            }
            const QString chargerKey = m_orderChargerKeys.take(session.orderId);
            if (!chargerKey.isEmpty()) {
                m_demoChargingChargerKeys.remove(chargerKey);
                const QString stationId = chargerKey.section(QLatin1Char('\n'), 0, 0);
                const QString chargerId = chargerKey.section(QLatin1Char('\n'), 1, 1);
                m_mapBinder->chargerStatusConfirmed(stationId, chargerId,
                                                    ChargerBusinessStatus::Idle);
                if (m_reservedDetailState.stationId == stationId) {
                    for (ChargerListItemView &charger : m_reservedDetailState.chargers) {
                        if (charger.chargerId != chargerId) continue;
                        charger.statusText = tr("空闲");
                        charger.canCharge = true;
                        charger.disabledReason.clear();
                        break;
                    }
                    renderStationDetailWithReservation(m_reservedDetailState);
                }
            }
            m_chargingSessionStates.removeAt(index);
            renderChargingSessions();
            renderHomeWithReservation(m_mapBinder->currentHomeState());
            m_settlement->render(m_settlementState);
            m_mainWindow->renderSecondaryPage(m_settlement);
            return;
        }
    });

    ReservationDemoFixture reservationFixture;
    QString reservationFixtureError;
    if (loadReservationDemoFixture(QStringLiteral(":/demo/reservation-demo-data.tmp"),
                                   &reservationFixture, &reservationFixtureError)) {
        m_reservationState.depositText = reservationFixture.depositText;
        m_reservationState.durationText = reservationFixture.durationText;
        m_reservationState.depositPolicyText = reservationFixture.depositPolicyText;
        m_reservationState.durationSeconds = reservationFixture.durationSeconds;
        m_reservationCancellationCooldownSeconds =
            reservationFixture.cancellationCooldownSeconds;
    } else {
        m_reservationState.status = ReservationConfirmationStatus::Error;
        m_reservationState.message = reservationFixtureError;
        m_reservationState.canRetry = true;
    }

    MockUserNetworkApi::Behavior profileBehavior;
    profileBehavior.delayMs = DemoDelayMs;
    m_network->setQueryBehavior(profileBehavior);
    m_network->setNicknameBehavior(profileBehavior);
    m_network->setLogoutBehavior(profileBehavior);

    // 配置连接必须排在 Binder 前面，保证 Mock 在请求发出前已得到本次行为。
    connect(m_login, &LoginWindow::loginRequested,
            this, &UserDemoController::configureLogin);
    connect(m_login, &LoginWindow::loginRequested,
            m_binder, &IUserUiBinder::loginRequested);
    connect(m_login, &LoginWindow::usernamePasswordLoginRequested,
            this, [this](const QString &username, const QString &password) {
        m_usernameFirstSetup = true;
        m_pendingUsername = username;
        m_pendingUsernamePassword = password;
        m_profileEditOpenedFromMain = false;
        m_profileEdit->setEditMode(ProfileEditMode::UsernameFirstSetup, username);
        ProfileEditViewState state;
        state.nicknameInput = username;
        state.canSubmit = true;
        m_profileEdit->render(state);
        showOnly(m_profileEdit);
    });
    connect(m_profileEdit, &ProfileEditWindow::profileSaveRequested,
            this, &UserDemoController::configureNicknameSave);
    connect(m_profileEdit, &ProfileEditWindow::profileSaveRequested,
            m_binder, &IUserUiBinder::profileSaveRequested);
    connect(m_profileEdit, &ProfileEditWindow::avatarChangeRequested,
            this, [this] {
        QString dataUri;
        QString error;
        QPixmap preview;
        if (!AvatarImageHelper::selectFromAlbum(m_profileEdit, &dataUri,
                                                 &preview, &error)) {
            if (!error.isEmpty())
                QMessageBox::warning(m_profileEdit, tr("更换头像"), error);
            return;
        }
        m_profileEdit->setAvatarPreview(dataUri);
        if (m_usernameFirstSetup) {
            m_pendingAvatarDataUri = dataUri;
            return;
        }
        m_binder->avatarUpdateRequested(dataUri);
    });
    connect(m_profileEdit, &ProfileEditWindow::profileCompletionRequested,
            this, [this](const QString &nickname, const QString &phone,
                         const QString &newPassword) {
        // 测试后端自动注册时以手机号作为默认资料；空昵称也必须落成
        // 非空值，否则资料保存请求会一直停留在“保存中”。
        const QString effectiveNickname = nickname.trimmed().isEmpty()
                                              ? phone.trimmed()
                                              : nickname.trimmed();
        if (m_usernameFirstSetup) {
            DemoUserData user;
            user.nickname = effectiveNickname;
            user.password = m_pendingUsernamePassword;
            user.avatarDataUri = m_pendingAvatarDataUri;
            user.status = AccountStatus::Normal;
            m_demoUsers.insert(phone, user);
            m_currentAccountKey = phone;
            m_usernameFirstSetup = false;
            m_pendingUsername.clear();
            m_pendingUsernamePassword.clear();
            ProfileViewState profile;
            profile.nickname = effectiveNickname;
            profile.maskedPhone = phone.left(3) + QStringLiteral("****") + phone.right(4);
            profile.balanceText = m_paymentBalanceText;
            profile.avatarDataUri = m_pendingAvatarDataUri;
            profile.accountState = AccountDisplayState::Normal;
            m_mainWindow->renderProfile(profile);
            m_pendingAvatarDataUri.clear();
            m_mainWindow->renderPrimaryPage(MainWindow::PrimaryPage::Home);
            showOnly(m_mainWindow);
            m_mapBinder->activateHome();
            return;
        }
        if (!newPassword.isEmpty())
            m_demoUsers[m_currentAccountKey].password = newPassword;
        configureNicknameSave(effectiveNickname);
        m_binder->profileSaveRequested(effectiveNickname);
    });
    connect(m_mainWindow, &MainWindow::logoutRequested,
            m_binder, &IUserUiBinder::logoutRequested);

    // The fixture file is only the initial server snapshot. Keep successful
    // registrations and profile updates in the Demo's runtime server state so
    // logout/relogin behaves like a real backend instead of replaying fixtures.
    connect(m_network, &IUserNetworkApi::loginSucceeded,
            this, &UserDemoController::rememberConfirmedLogin);
    connect(m_network, &IUserNetworkApi::nicknameUpdateSucceeded,
            this, &UserDemoController::rememberConfirmedNickname);
    connect(m_network, &IUserNetworkApi::avatarUpdateSucceeded,
            this, &UserDemoController::rememberConfirmedAvatar);

    connect(m_binder, &IUserUiBinder::loginViewStateChanged,
            m_login, &LoginWindow::render);
    connect(m_binder, &IUserUiBinder::profileEditViewStateChanged,
            m_profileEdit, &ProfileEditWindow::render);
    connect(m_binder, &IUserUiBinder::profileViewStateChanged,
            m_mainWindow, &MainWindow::renderProfile);
    connect(m_binder, &IUserUiBinder::navigationRequested,
            this, &UserDemoController::handleNavigation);

    connect(m_mainWindow, &MainWindow::primaryPageRequested,
            m_mainWindow, &MainWindow::renderPrimaryPage);
    connect(m_mainWindow, &MainWindow::profileEditRequested,
            this, [this] {
        m_profileEditOpenedFromMain = true;
        m_profileEdit->setEditMode(ProfileEditMode::ExistingProfile);
        m_profileEdit->render(m_binder->currentProfileEditViewState());
        showOnly(m_profileEdit);
    });
    connect(m_profileEdit, &ProfileEditWindow::backRequested,
            this, [this] {
        if (m_usernameFirstSetup) {
            m_usernameFirstSetup = false;
            m_pendingUsername.clear();
            m_pendingUsernamePassword.clear();
            showOnly(m_login);
            return;
        }
        if (m_profileEditOpenedFromMain) {
            showOnly(m_mainWindow);
            return;
        }
        // 新用户资料尚未完善时不能绕过该步骤进入首页；返回即放弃本次
        // 已认证会话，由既有流程统一清理状态并导航回登录页。
        m_binder->logoutRequested();
    });
    connect(m_profileEdit, &ProfileEditWindow::passwordChangeRequested,
            this, [this] {
        bool accepted = false;
        const QString password = QInputDialog::getText(
            m_profileEdit, tr("验证原密码"),
            tr("请输入当前登录密码"), QLineEdit::Password,
            QString(), &accepted);
        if (!accepted)
            return;
        if (password.isEmpty()) {
            QMessageBox::warning(m_profileEdit, tr("无法验证"),
                                 tr("请输入原密码。"));
            return;
        }
        if (m_demoUsers.value(m_currentAccountKey).password != password) {
            QMessageBox::warning(m_profileEdit, tr("验证失败"),
                                 tr("原密码不正确，请重新输入。"));
            return;
        }
        QMessageBox::information(m_profileEdit, tr("验证成功"),
                                 tr("原密码验证正确，即将进入密码修改页面。"));
        m_passwordChange->setGeometry(m_profileEdit->geometry());
        m_passwordChange->setStep(PasswordChangeStep::EnterNewPassword);
        showOnly(m_passwordChange);
    });
    connect(m_passwordChange, &PasswordChangeWindow::backRequested,
            this, [this] {
        m_profileEdit->setEditMode(ProfileEditMode::ExistingProfile);
        showOnly(m_profileEdit);
    });
    connect(m_passwordChange, &PasswordChangeWindow::originalPasswordSubmitted,
            this, [this](const QString &password) {
        if (m_demoUsers.value(m_currentAccountKey).password != password) {
            m_passwordChange->setStep(PasswordChangeStep::VerifyOriginal,
                                      tr("原密码不正确，请重新输入"));
            return;
        }
        QMessageBox::information(m_passwordChange, tr("验证成功"),
                                 tr("原密码验证正确，请继续设置新密码。"));
        m_passwordChange->setStep(PasswordChangeStep::EnterNewPassword);
    });
    connect(m_passwordChange, &PasswordChangeWindow::newPasswordSubmitted,
            this, [this](const QString &password) {
        m_demoUsers[m_currentAccountKey].password = password;
        QMessageBox::information(m_passwordChange, tr("修改成功"),
                                 tr("登录密码已修改。"));
        m_profileEdit->setEditMode(ProfileEditMode::ExistingProfile);
        showOnly(m_profileEdit);
    });

    connect(m_mainWindow, &MainWindow::locateRequested,
            m_mapBinder, &IMapUiBinder::locateRequested);
    connect(m_mainWindow, &MainWindow::mapReady,
            m_mapBinder, &IMapUiBinder::mapReady);
    connect(m_mainWindow, &MainWindow::mapLoadFailed,
            m_mapBinder, &IMapUiBinder::mapLoadFailed);
    connect(m_mainWindow, &MainWindow::stationSearchRequested,
            m_mapBinder, &IMapUiBinder::stationSearchRequested);
    connect(m_mainWindow, &MainWindow::stationSearchRetryRequested,
            m_mapBinder, &IMapUiBinder::stationSearchRetryRequested);
    connect(m_mainWindow, &MainWindow::stationSearchCleared,
            m_mapBinder, &IMapUiBinder::stationSearchCleared);
    connect(m_mainWindow, &MainWindow::searchAreaRequested,
            m_mapBinder, &IMapUiBinder::searchAreaRequested);
    connect(m_mainWindow, &MainWindow::stationSelected,
            m_mapBinder, &IMapUiBinder::stationSelected);
    connect(m_mainWindow, &MainWindow::stationDetailsRequested,
            m_mapBinder, &IMapUiBinder::stationDetailsRequested);
    connect(m_stationDetail, &StationDetailWindow::backRequested,
            m_mapBinder, &IMapUiBinder::backRequested);
    connect(m_stationDetail, &StationDetailWindow::stationRefreshRequested,
            m_mapBinder, &IMapUiBinder::stationRefreshRequested);
    connect(m_stationDetail, &StationDetailWindow::chargerSelected,
            m_mapBinder, &IMapUiBinder::chargerSelected);
    connect(m_stationDetail, &StationDetailWindow::routePreviewRequested,
            m_mapBinder, &IMapUiBinder::routePreviewRequested);
    connect(m_stationDetail, &StationDetailWindow::chargeConfirmationRequested,
            this, [this](const QString &stationId, const QString &chargerId) {
        m_scannerOpenedFromCharging = false;
        m_scanState.expectedStationId = stationId;
        m_scanState.expectedChargerId = chargerId;
        m_scanState.chargerDisplayText = tr("充电桩 %1").arg(
            chargerId.section(QLatin1Char('-'), -1).toUpper());
        m_scanState.status = m_qrScanner->cameraAvailable() ? ScanStatus::RequestingPermission : ScanStatus::Error;
        m_scanState.cameraAvailable = m_qrScanner->cameraAvailable();
        m_scanState.cameraPermissionGranted = false;
        m_scanState.canRetry = false;
        m_scanState.canImportImage = true;
        m_scanState.message = m_scanState.cameraAvailable
            ? tr("点击允许摄像头后开始实时扫码")
            : tr("Demo 没有可用摄像头，可从相册选择二维码进行流程测试");
        m_qrScanner->render(m_scanState);
        m_mainWindow->renderSecondaryPage(m_qrScanner);
    });
    connect(m_qrScanner, &QrCodeScannerWindow::backRequested,
            this, [this] {
        if (m_scannerOpenedFromOrders) {
            m_scannerOpenedFromOrders = false;
            m_scannerOpenedFromCharging = false;
            m_orderList->render(m_orderListState);
            m_mainWindow->renderSecondaryPage(m_orderList);
            return;
        }
        if (m_scannerOpenedFromCharging)
            m_mainWindow->renderPrimaryPage(MainWindow::PrimaryPage::Charging);
        else
            m_mainWindow->renderSecondaryPage(m_stationDetail);
    });
    connect(m_qrScanner, &QrCodeScannerWindow::cameraPermissionRequested,
            this, [this] {
        m_scanState.status = m_qrScanner->cameraAvailable() ? ScanStatus::Scanning : ScanStatus::Error;
        m_scanState.cameraPermissionGranted = m_qrScanner->cameraAvailable();
        m_scanState.message = m_scanState.cameraPermissionGranted
            ? tr("对准二维码后将自动识别") : tr("当前设备没有可用摄像头");
        m_qrScanner->render(m_scanState);
    });
    connect(m_chargingSession, &ChargingSessionWindow::scanChargingRequested,
            this, [this] {
        m_scannerOpenedFromCharging = true;
        m_scanState.expectedStationId.clear();
        m_scanState.expectedChargerId.clear();
        for (const StationDetail &station : m_demoStations) {
            for (const ChargerSummary &charger : station.chargers) {
                const QString key = station.stationId + QLatin1Char('\n') + charger.chargerId;
                if (!charger.canStartCharging || m_demoChargingChargerKeys.contains(key))
                    continue;
                m_scanState.expectedStationId = station.stationId;
                m_scanState.expectedChargerId = charger.chargerId;
                break;
            }
            if (!m_scanState.expectedChargerId.isEmpty()) break;
        }
        m_scanState.chargerDisplayText = tr("等待识别充电桩二维码");
        m_scanState.status = m_qrScanner->cameraAvailable() ? ScanStatus::RequestingPermission : ScanStatus::Error;
        m_scanState.cameraAvailable = m_qrScanner->cameraAvailable();
        m_scanState.cameraPermissionGranted = false;
        m_scanState.canRetry = false;
        m_scanState.canImportImage = !m_scanState.expectedChargerId.isEmpty();
        m_scanState.message = m_scanState.cameraAvailable
            ? tr("点击允许摄像头后开始实时扫码")
            : m_scanState.canImportImage
            ? tr("Demo 没有可用摄像头，可从相册选择二维码进行流程测试")
            : tr("现有站点数据中没有可启动的充电桩");
        m_qrScanner->render(m_scanState);
        m_mainWindow->renderSecondaryPage(m_qrScanner);
    });
    connect(m_qrScanner, &QrCodeScannerWindow::qrCodeDetected,
            this, [this](const QString &rawText) {
        const QString code = chargerCodeFromQr(rawText);
        if (code.isEmpty()) {
            m_scanState.status = ScanStatus::Error;
            m_scanState.message = tr("二维码内容不包含合法的 chargerCode。");
            m_scanState.canRetry = true;
            m_scanState.canImportImage = true;
            m_qrScanner->render(m_scanState);
            return;
        }
        for (const StationDetail &station : m_demoStations) {
            for (const ChargerSummary &charger : station.chargers) {
                if (charger.chargerId.compare(code, Qt::CaseInsensitive) != 0)
                    continue;
                if (!charger.canStartCharging) {
                    m_scanState.status = ScanStatus::Error;
                    m_scanState.message = charger.disabledReason.isEmpty()
                        ? tr("该电桩当前不可启动充电。") : charger.disabledReason;
                    m_scanState.canRetry = true;
                    m_scanState.canImportImage = true;
                    m_qrScanner->render(m_scanState);
                    return;
                }
                m_scanState.expectedStationId = station.stationId;
                m_scanState.expectedChargerId = charger.chargerId;
                m_scanState.chargerDisplayText = charger.chargerId;
                m_scanState.status = ScanStatus::Validating;
                m_scanState.message = tr("二维码识别成功，正在加载充电确认信息…");
                m_scanState.canImportImage = false;
                m_qrScanner->render(m_scanState);
                showChargeConfirmation(station.stationId, charger.chargerId);
                return;
            }
        }
        m_scanState.status = ScanStatus::Error;
        m_scanState.message = tr("Demo 数据中找不到该电桩。");
        m_scanState.canRetry = true;
        m_scanState.canImportImage = true;
        m_qrScanner->render(m_scanState);
    });
    connect(m_qrScanner, &QrCodeScannerWindow::imageImportRequested,
            this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            m_qrScanner, tr("选择二维码图片"), QString(),
            tr("图片 (*.png *.jpg *.jpeg *.bmp)"));
        if (path.isEmpty()) return;
        QString error;
        const QString code = chargerCodeFromQr(decodeQrImage(path, &error));
        if (code.isEmpty()) {
            m_scanState.status = ScanStatus::Error;
            m_scanState.message = error.isEmpty()
                ? tr("二维码内容不包含合法的 chargerCode。") : error;
            m_scanState.canImportImage = true;
            m_qrScanner->render(m_scanState);
            return;
        }
        for (const StationDetail &station : m_demoStations) {
            for (const ChargerSummary &charger : station.chargers) {
                if (charger.chargerId.compare(code, Qt::CaseInsensitive) != 0)
                    continue;
                if (!charger.canStartCharging) {
                    m_scanState.status = ScanStatus::Error;
                    m_scanState.message = charger.disabledReason.isEmpty()
                        ? tr("该电桩当前不可启动充电。") : charger.disabledReason;
                    m_scanState.canImportImage = true;
                    m_qrScanner->render(m_scanState);
                    return;
                }
                m_scanState.expectedStationId = station.stationId;
                m_scanState.expectedChargerId = charger.chargerId;
                m_scanState.chargerDisplayText = charger.chargerId;
                m_scanState.status = ScanStatus::Validating;
                m_scanState.message = tr("二维码识别成功，正在加载充电确认信息…");
                m_scanState.canImportImage = false;
                m_qrScanner->render(m_scanState);
                showChargeConfirmation(station.stationId, charger.chargerId);
                return;
            }
        }
        m_scanState.status = ScanStatus::Error;
        m_scanState.message = tr("Demo 数据中找不到该电桩。");
        m_scanState.canImportImage = true;
        m_qrScanner->render(m_scanState);
    });
    connect(m_qrScanner, &QrCodeScannerWindow::scanRetryRequested,
            this, [this] {
        m_scanState.cameraAvailable = m_qrScanner->cameraAvailable();
        m_scanState.cameraPermissionGranted = m_scanState.cameraAvailable;
        m_scanState.status = m_scanState.cameraAvailable ? ScanStatus::Scanning
                                                         : ScanStatus::Error;
        m_scanState.message = m_scanState.cameraAvailable
            ? tr("正在重新打开摄像头…") : tr("当前没有可用摄像头");
        m_qrScanner->render(m_scanState);
    });
    connect(m_stationDetail, &StationDetailWindow::reservationConfirmationRequested,
            this, [this](const QString &stationId, const QString &chargerId) {
        const StationDetailViewState detail = m_mapBinder->currentStationDetailState();
        m_reservationState.stationId = stationId;
        m_reservationState.chargerId = chargerId;
        m_reservationState.stationName = detail.name;
        m_reservationState.stationAddress = detail.address;
        for (const ChargerListItemView &charger : detail.chargers) {
            if (charger.chargerId != chargerId) continue;
            m_reservationState.chargerCode = charger.chargerId.section(
                QLatin1Char('-'), -1).toUpper();
            m_reservationState.chargerTypeText = charger.title;
            m_reservationState.powerText = charger.powerText;
            break;
        }
        m_reservationState.status = ReservationConfirmationStatus::Ready;
        m_reservationState.canReserve = true;
        m_reservationState.canRetry = false;
        m_reservationState.message.clear();
        m_reservationConfirmation->render(m_reservationState);
        m_mainWindow->renderSecondaryPage(m_reservationConfirmation);
    });
    connect(m_reservationConfirmation, &ReservationConfirmationWindow::backRequested,
            this, [this] { m_mainWindow->renderSecondaryPage(m_stationDetail); });
    connect(m_reservationConfirmation,
            &ReservationConfirmationWindow::reservationRefreshRequested,
            m_reservationBinder, &ReservationUiBinder::refreshRequested);
    connect(m_reservationBinder, &ReservationUiBinder::stateChanged,
            this, [this](const ReservationConfirmationViewState &state) {
        m_reservationState = state;
        m_reservationConfirmation->render(state);
        if (m_paymentState.purpose == PaymentPurpose::Reservation
            && (state.status == ReservationConfirmationStatus::Error
                || state.status == ReservationConfirmationStatus::ResultUnknown)) {
            m_paymentState.status = state.status == ReservationConfirmationStatus::ResultUnknown
                ? PaymentViewStatus::ResultUnknown : PaymentViewStatus::Error;
            m_paymentState.canPay = false;
            m_paymentState.message = state.message;
            m_payment->render(m_paymentState);
            m_mainWindow->renderSecondaryPage(m_payment);
        }
    });
    connect(m_reservationBinder, &ReservationUiBinder::activeReservationChanged,
            this, [this](const std::optional<ActiveReservationView> &reservation) {
        if (!reservation) {
            if (!m_reservedDetailState.activeReservation) return;
            const ActiveReservationView previous = *m_reservedDetailState.activeReservation;
            const bool cancelled = previous.cancellationStatus
                                   == ReservationCancellationStatus::Submitting;
            m_mapBinder->chargerStatusConfirmed(previous.stationId, previous.chargerId,
                                                ChargerBusinessStatus::Idle);
            m_reservedDetailState.activeReservation.reset();
            m_reservedDetailState.selectedChargerId.clear();
            m_reservedDetailState.canContinueToConfirmation = false;
            if (cancelled) {
                m_reservedDetailState.canCreateReservation = false;
                m_reservedDetailState.reservationDisabledReason =
                    tr("刚刚取消过预约，%1 秒后可再次预约")
                        .arg(m_reservationCancellationCooldownSeconds);
                const QString accountKey = m_currentAccountKey;
                m_reservationCancellationLockedAccounts.insert(accountKey);
                QTimer::singleShot(m_reservationCancellationCooldownSeconds * 1000,
                                   this, [this, accountKey] {
                    m_reservationCancellationLockedAccounts.remove(accountKey);
                    if (m_currentAccountKey != accountKey) return;
                    m_reservedDetailState.canCreateReservation = true;
                    m_reservedDetailState.reservationDisabledReason.clear();
                    renderStationDetailWithReservation(m_reservedDetailState);
                });
            }
            for (OrderListItemView &order : m_orderListState.orders) {
                if (order.type != OrderBusinessType::Reservation
                    || order.businessId != previous.reservationId) continue;
                order.statusText = cancelled ? tr("已退回") : tr("已超时");
                order.statusTone = QStringLiteral("neutral");
                order.summaryText = cancelled
                    ? tr("预约已取消 · 押金已退回钱包")
                    : tr("预约已超时");
                order.actionText = tr("查看详情");
                order.action = OrderListAction::ViewDetails;
                break;
            }
            renderStationDetailWithReservation(m_reservedDetailState);
            renderHomeWithReservation(m_mapBinder->currentHomeState());
            return;
        }
        const ActiveReservationView active = *reservation;
        m_reservedDetailState = m_mapBinder->currentStationDetailState();
        m_reservedDetailState.selectedChargerId = active.chargerId;
        m_reservedDetailState.canContinueToConfirmation = true;
        m_reservedDetailState.activeReservation = active;
        m_mapBinder->chargerStatusConfirmed(active.stationId, active.chargerId,
                                            ChargerBusinessStatus::Reserved);
        const bool exists = std::any_of(
            m_orderListState.orders.cbegin(), m_orderListState.orders.cend(),
            [&active](const OrderListItemView &item) {
                return item.type == OrderBusinessType::Reservation
                       && item.businessId == active.reservationId;
            });
        if (!exists) {
            OrderListItemView order;
            order.businessId = active.reservationId;
            order.stationId = active.stationId;
            order.chargerId = active.chargerId;
            order.type = OrderBusinessType::Reservation;
            order.stationName = m_reservationState.stationName;
            order.chargerCode = m_reservationState.chargerCode;
            order.createdAtText = tr("刚刚");
            order.summaryText = tr("预约时长 %1 · 押金已支付")
                                    .arg(m_reservationState.durationText);
            order.amountText = m_reservationState.depositText;
            order.statusText = tr("已预约");
            order.statusTone = QStringLiteral("info");
            order.actionText = tr("扫码充电");
            order.action = OrderListAction::StartReservedCharging;
            m_orderListState.orders.prepend(order);
        }
        renderStationDetailWithReservation(m_reservedDetailState);
        renderHomeWithReservation(m_mapBinder->currentHomeState());
        m_pendingReservationStationId.clear();
        m_pendingReservationChargerId.clear();
        m_pendingReservationDurationSeconds = 0;
    });
    connect(m_reservationConfirmation, &ReservationConfirmationWindow::reserveRequested,
            this, [this](const QString &stationId, const QString &chargerId,
                         int durationSeconds) {
        if (m_reservedDetailState.activeReservation.has_value()) {
            m_reservationState.status = ReservationConfirmationStatus::Error;
            m_reservationState.canReserve = false;
            m_reservationState.canRetry = false;
            m_reservationState.message = QStringLiteral("当前账号已有未结束的预约");
            m_reservationConfirmation->render(m_reservationState);
            return;
        }
        m_pendingReservationStationId = stationId;
        m_pendingReservationChargerId = chargerId;
        m_pendingReservationDurationSeconds = durationSeconds;
        m_paymentState = PaymentViewState{};
        m_paymentState.businessId = QStringLiteral("reservation:%1:%2").arg(stationId, chargerId);
        m_paymentState.purpose = PaymentPurpose::Reservation;
        m_paymentState.titleText = tr("预约支付");
        m_paymentState.descriptionText = tr("%1 · %2号桩预约押金")
                                             .arg(m_reservationState.stationName,
                                                  m_reservationState.chargerCode);
        m_paymentState.amountText = m_reservationState.depositText;
        m_paymentState.balanceText = m_paymentBalanceText;
        m_paymentState.balanceAfterPaymentText =
            balanceAfterPaymentText(m_paymentBalanceCents,
                                    m_paymentState.amountText);
        const qint64 reservationAmountCents = moneyTextToCents(m_paymentState.amountText);
        m_paymentState.canPay = reservationAmountCents >= 0
                                && m_paymentBalanceCents >= reservationAmountCents;
        m_paymentState.canRecharge = true;
        if (!m_paymentState.canPay)
            m_paymentState.message = tr("钱包余额不足，请先充值后再支付。");
        m_payment->render(m_paymentState);
        m_mainWindow->renderSecondaryPage(m_payment);
    });
    connect(m_stationDetail, &StationDetailWindow::reservationExpiredRefreshRequested,
            this, [this] {
        if (!m_reservedDetailState.activeReservation.has_value()) return;
        m_reservationBinder->expireReservationIfNeeded();
        QMessageBox::warning(m_stationDetail, tr("预约已超时"),
                             tr("预约时间已结束，未按时开始充电将按预约规则扣除押金。"));
    });
    connect(m_stationDetail, &StationDetailWindow::activeReservationRequested,
            this, [this](const QString &reservationId, const QString &stationId,
                         const QString &chargerId) {
        if (!m_reservedDetailState.activeReservation
            || m_reservedDetailState.activeReservation->reservationId != reservationId) {
            QMessageBox::warning(m_stationDetail, tr("预约状态已变化"),
                                 tr("未找到该预约，请刷新后重试。"));
            return;
        }
        m_pendingReservationFocusChargerId = chargerId;
        // 始终通过 Binder 进入详情，使其页面状态同步为 StationDetail；
        // 否则从首页预约卡直接显示控件后，返回意图会被当作“已在首页”而忽略。
        m_mapBinder->stationDetailsRequested(stationId);
    });
    connect(m_stationDetail, &StationDetailWindow::cancelReservationRequested,
            this, [this](const QString &reservationId) {
        if (QMessageBox::question(
                m_stationDetail, tr("取消预约"),
                tr("确定取消当前预约吗？频繁预约和取消可能会被限制预约。"))
            != QMessageBox::Yes)
            return;
        m_reservationBinder->cancelReservationRequested(reservationId);
    });
    connect(m_stationDetail, &StationDetailWindow::cancelReservationRetryRequested,
            m_reservationBinder, &ReservationUiBinder::cancelReservationRetryRequested);
    connect(m_chargeConfirmation, &ChargeConfirmationWindow::backRequested,
            this, [this] {
        if (m_scannerOpenedFromOrders) {
            m_scannerOpenedFromOrders = false;
            m_scannerOpenedFromCharging = false;
            m_orderList->render(m_orderListState);
            m_mainWindow->renderSecondaryPage(m_orderList);
            return;
        }
        if (m_scannerOpenedFromCharging)
            m_mainWindow->renderPrimaryPage(MainWindow::PrimaryPage::Charging);
        else
            m_mainWindow->renderSecondaryPage(m_stationDetail);
    });
    connect(m_chargeConfirmation, &ChargeConfirmationWindow::confirmationRefreshRequested,
            this, [this] { m_chargeConfirmation->render(m_chargeConfirmationState); });
    connect(m_chargeConfirmation, &ChargeConfirmationWindow::rechargeRequested,
            this, [this] {
        m_walletOpenedFromConfirmation = true;
        m_walletRecharge->renderBalance(m_chargeConfirmationState.walletBalanceText);
        m_mainWindow->renderSecondaryPage(m_walletRecharge);
    });
    connect(m_chargeConfirmation, &ChargeConfirmationWindow::startChargingRequested,
            this, [this](const QString &stationId, const QString &chargerId) {
        ChargeConfirmationViewState submitting = m_chargeConfirmationState;
        submitting.status = ChargeConfirmationStatus::Submitting;
        submitting.canStart = false;
        m_chargeConfirmation->render(submitting);
        QTimer::singleShot(DemoDelayMs, this, [this, stationId, chargerId] {
            if (m_chargingSessionDemoTemplates.isEmpty()) return;
            ChargingSessionViewState session = m_chargingSessionDemoTemplates.at(
                m_chargingSessionStates.size() % m_chargingSessionDemoTemplates.size());
            session.orderId = QStringLiteral("demo-scan-%1")
                                  .arg(m_chargingSessionStates.size() + 1);
            if (!m_chargeConfirmationState.stationName.isEmpty())
                session.stationName = m_chargeConfirmationState.stationName;
            if (!m_chargeConfirmationState.chargerCode.isEmpty())
                session.chargerCode = m_chargeConfirmationState.chargerCode;
            session.chargerTypeText = m_chargeConfirmationState.chargerTypeText;
            session.ratedPowerText = m_chargeConfirmationState.powerText;
            m_chargingSessionStates.append(session);
            const QString chargerKey = stationId + QLatin1Char('\n') + chargerId;
            QString consumedReservationId;
            if (m_reservedDetailState.activeReservation
                && m_reservedDetailState.activeReservation->stationId == stationId
                && m_reservedDetailState.activeReservation->chargerId == chargerId)
                consumedReservationId = m_reservedDetailState.activeReservation->reservationId;
            if (consumedReservationId.isEmpty()) {
                for (const OrderListItemView &order : m_orderListState.orders) {
                    if (order.type == OrderBusinessType::Reservation
                        && order.stationId == stationId && order.chargerId == chargerId
                        && order.action == OrderListAction::StartReservedCharging) {
                        consumedReservationId = order.businessId;
                        break;
                    }
                }
            }
            const bool consumedReservation = !consumedReservationId.isEmpty();
            m_demoChargingChargerKeys.insert(chargerKey);
            m_mapBinder->chargerStatusConfirmed(stationId, chargerId,
                                                ChargerBusinessStatus::Charging);
            m_orderChargerKeys.insert(session.orderId, chargerKey);
            if (m_reservedDetailState.stationId == stationId) {
                for (ChargerListItemView &charger : m_reservedDetailState.chargers) {
                    if (charger.chargerId != chargerId) continue;
                    charger.statusText = tr("充电中");
                    charger.canCharge = false;
                    charger.disabledReason = tr("该充电桩正在为当前账号充电");
                    break;
                }
            }
            // Keep one occupied key across idle→charging and reserved→charging.
            // Home rendering de-duplicates it with an active reservation.
            if (consumedReservation) {
                m_reservationBinder->consumeActiveReservation();
                if (m_reservedDetailState.activeReservation
                    && m_reservedDetailState.activeReservation->reservationId == consumedReservationId)
                    m_reservedDetailState.activeReservation.reset();
                for (OrderListItemView &order : m_orderListState.orders) {
                    if (order.businessId != consumedReservationId
                        || order.type != OrderBusinessType::Reservation) continue;
                    order.statusText = tr("充电中");
                    order.statusTone = QStringLiteral("success");
                    order.summaryText = tr("预约已使用 · 正在充电");
                    order.actionText = tr("查看充电");
                    order.action = OrderListAction::ViewCharging;
                    order.relatedBusinessId = session.orderId;
                    break;
                }
            }
            // Demo uses the map fixture as its server substitute. Re-render the
            // authoritative home snapshot with the newly occupied charger so
            // the station row changes immediately even while the page is hidden.
            renderHomeWithReservation(m_mapBinder->currentHomeState());
            renderChargingSessions(session.orderId);
            m_scannerOpenedFromOrders = false;
            m_mainWindow->renderPrimaryPage(MainWindow::PrimaryPage::Charging);
        });
    });
    connect(m_navigation, &NavigationWindow::backRequested,
            m_mapBinder, &IMapUiBinder::backRequested);
    connect(m_navigation, &NavigationWindow::routeModeRequested,
            m_mapBinder, &IMapUiBinder::routeModeRequested);
    connect(m_navigation, &NavigationWindow::manualOriginRequested,
            m_mapBinder, &IMapUiBinder::manualOriginRequested);
    connect(m_navigation, &NavigationWindow::originCandidateSelected,
            m_mapBinder, &IMapUiBinder::originCandidateSelected);
    connect(m_navigation, &NavigationWindow::routeRetryRequested,
            m_mapBinder, &IMapUiBinder::routeRetryRequested);

    connect(m_settlement, &SettlementWindow::backRequested,
            this, [this] { m_mainWindow->renderPrimaryPage(MainWindow::PrimaryPage::Charging); });
    connect(m_settlement, &SettlementWindow::paymentRequested,
            this, [this](const QString &orderId) {
        m_paymentState = PaymentViewState{};
        m_paymentState.businessId = orderId;
        m_paymentState.purpose = PaymentPurpose::ChargingSettlement;
        m_paymentState.titleText = tr("订单支付");
        m_paymentState.descriptionText = tr("%1 · %2号桩充电订单")
                                             .arg(m_settlementState.stationName,
                                                  m_settlementState.chargerCode);
        m_paymentState.amountText = m_settlementState.payableText;
        m_paymentState.balanceText = m_paymentBalanceText;
        m_paymentState.balanceAfterPaymentText =
            balanceAfterPaymentText(m_paymentBalanceCents,
                                    m_paymentState.amountText);
        const qint64 settlementAmountCents = moneyTextToCents(m_paymentState.amountText);
        m_paymentState.canPay = settlementAmountCents >= 0
                                && m_paymentBalanceCents >= settlementAmountCents;
        m_paymentState.canRecharge = true;
        if (!m_paymentState.canPay)
            m_paymentState.message = tr("钱包余额不足，请先充值后再支付。");
        m_payment->render(m_paymentState);
        m_mainWindow->renderSecondaryPage(m_payment);
    });
    connect(m_payment, &PaymentWindow::backRequested, this, [this] {
        if (m_paymentState.purpose == PaymentPurpose::ChargingSettlement)
            m_mainWindow->renderSecondaryPage(m_settlement);
        else
            m_mainWindow->renderSecondaryPage(m_reservationConfirmation);
    });
    connect(m_payment, &PaymentWindow::rechargeRequested, this, [this] {
        m_walletOpenedFromPayment = true;
        m_walletOpenedFromConfirmation = false;
        m_walletRecharge->renderBalance(m_paymentBalanceText);
        m_mainWindow->renderSecondaryPage(m_walletRecharge);
    });
    connect(m_payment, &PaymentWindow::payRequested,
            this, [this](const QString &businessId, PaymentPurpose purpose) {
        if (m_paymentOperationActive) return;
        const qint64 amountCents = moneyTextToCents(m_paymentState.amountText);
        if (amountCents < 0 || m_paymentBalanceCents < amountCents) {
            m_paymentState.canPay = false;
            m_paymentState.message = tr("钱包余额不足，请先充值后再支付。");
            m_payment->render(m_paymentState);
            return;
        }
        m_paymentOperationActive = true;
        PaymentViewState submitting = m_paymentState;
        submitting.status = PaymentViewStatus::Submitting;
        submitting.canPay = false;
        submitting.message = tr("正在确认支付结果，请勿重复提交…");
        m_payment->render(submitting);
        QTimer::singleShot(m_paymentDelayMs, this, [this, businessId, purpose, amountCents] {
            if (m_paymentOutcome == QStringLiteral("failure")) {
                m_paymentOperationActive = false;
                m_paymentState.status = PaymentViewStatus::Error;
                m_paymentState.message = tr("支付失败，未扣除余额，请稍后重试。");
                m_paymentState.canPay = true;
                m_payment->render(m_paymentState);
                return;
            }
            if (m_paymentOutcome == QStringLiteral("processing")) {
                m_paymentState.status = PaymentViewStatus::Submitting;
                m_paymentState.canPay = false;
                m_paymentState.canRecharge = false;
                m_paymentState.message = tr("支付处理中，请勿关闭页面或重复支付。");
                m_payment->render(m_paymentState);
                return;
            }
            if (m_paymentOutcome == QStringLiteral("result_unknown")) {
                m_paymentState.status = PaymentViewStatus::ResultUnknown;
                m_paymentState.canPay = false;
                m_paymentState.canRecharge = false;
                m_paymentState.canRecoverResult = true;
                m_paymentState.message = tr("支付结果暂时未知，正在查询原支付操作，请勿重复支付。");
                m_payment->render(m_paymentState);
                return;
            }
            m_paymentOperationActive = false;
            m_paymentBalanceCents -= amountCents;
            m_paymentBalanceText = QStringLiteral("¥%1")
                                       .arg(m_paymentBalanceCents / 100.0, 0, 'f', 2);
            if (purpose == PaymentPurpose::Reservation) {
                if (businessId != m_paymentState.businessId
                    || m_pendingReservationStationId.isEmpty()
                    || m_pendingReservationChargerId.isEmpty()) return;
                m_reservationBinder->setConfirmationState(m_reservationState);
                m_reservationBinder->reserveRequested(
                    m_pendingReservationStationId,
                    m_pendingReservationChargerId,
                    m_pendingReservationDurationSeconds);
            } else {
                for (OrderListItemView &order : m_orderListState.orders) {
                    if (order.businessId != businessId
                        || order.type != OrderBusinessType::Charging) continue;
                    order.statusText = tr("已完成");
                    order.statusTone = QStringLiteral("success");
                    order.actionText = tr("查看详情");
                    order.action = OrderListAction::ViewDetails;
                    break;
                }
            }
            m_paymentState.status = PaymentViewStatus::Success;
            m_paymentState.canPay = false;
            m_paymentState.message = tr("支付成功");
            m_payment->render(m_paymentState);
            QTimer::singleShot(500, this, [this] {
                m_mainWindow->renderPrimaryPage(MainWindow::PrimaryPage::Home);
            });
        });
    });
    connect(m_payment, &PaymentWindow::paymentResultRefreshRequested,
            this, [this](const QString &businessId) {
        if (businessId != m_paymentState.businessId) return;
        m_paymentState.status = PaymentViewStatus::Submitting;
        m_paymentState.canRecoverResult = false;
        m_paymentState.message = tr("正在查询原支付操作结果…");
        m_payment->render(m_paymentState);
        QTimer::singleShot(m_paymentDelayMs, this, [this] {
            m_paymentState.status = PaymentViewStatus::ResultUnknown;
            m_paymentState.canRecoverResult = true;
            m_paymentState.message = tr("暂未查到最终结果，请稍后继续查询，切勿重新支付。");
            m_payment->render(m_paymentState);
        });
    });

    connect(m_mapBinder, &IMapUiBinder::homeStateChanged,
            this, &UserDemoController::renderHomeWithReservation);
    connect(m_mapBinder, &IMapUiBinder::stationDetailStateChanged,
            this, &UserDemoController::renderStationDetailWithReservation);
    connect(m_mapBinder, &IMapUiBinder::navigationStateChanged,
            m_navigation, &NavigationWindow::render);
    connect(m_mapBinder, &IMapUiBinder::pageRequested,
            this, &UserDemoController::handleMapPage);
    connect(m_mainWindow, &MainWindow::rechargePageRequested,
            this, [this] {
        m_walletOpenedFromConfirmation = false;
        m_walletRecharge->renderBalance(
            m_binder->currentProfileViewState().balanceText);
        m_mainWindow->renderSecondaryPage(m_walletRecharge);
    });
    connect(m_mainWindow, &MainWindow::ordersPageRequested, this, [this] {
        m_orderList->render(m_orderListState);
        m_mainWindow->renderSecondaryPage(m_orderList);
    });
    connect(m_mainWindow, &MainWindow::frequentStationsRequested, this, [this] {
        QHash<QString, FrequentStationItemView> aggregated;
        for (const OrderListItemView &order : m_orderListState.orders) {
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
        if (state.stations.isEmpty())
            state.message = tr("完成充电或预约后，常用充电站会显示在这里");
        m_frequentStations->render(state);
        m_mainWindow->renderSecondaryPage(m_frequentStations);
    });
    connect(m_mainWindow, &MainWindow::feedbackRequested, this, [this] {
        m_profileText->renderContent(
            tr("帮助与反馈"),
            tr("如果您在查找充电站、预约、扫码充电、订单支付或钱包使用过程中遇到问题，请记录发生时间、充电站名称和页面提示。当前版本为演示界面，后续接入服务端后将提供反馈提交、处理进度和历史反馈查询功能。"));
        m_mainWindow->renderSecondaryPage(m_profileText);
    });
    connect(m_mainWindow, &MainWindow::aboutRequested, this, [this] {
        m_profileText->renderContent(
            tr("关于智充"),
            tr("智充是一款面向新能源汽车用户的充电服务应用，提供附近充电站查询、路线规划、充电桩预约、扫码充电、实时进度、订单结算和钱包服务。我们希望让充电信息更清晰，让每一次出发都充满能量。当前展示版本用于客户端 UI 与服务端接口联调。"));
        m_mainWindow->renderSecondaryPage(m_profileText);
    });
    connect(m_frequentStations, &FrequentStationsWindow::backRequested,
            this, [this] {
        m_mainWindow->renderPrimaryPage(MainWindow::PrimaryPage::Profile);
    });
    connect(m_frequentStations, &FrequentStationsWindow::stationRequested,
            m_mapBinder, &IMapUiBinder::stationDetailsRequested);
    connect(m_profileText, &ProfileTextWindow::backRequested,
            this, [this] {
        m_mainWindow->renderPrimaryPage(MainWindow::PrimaryPage::Profile);
    });
    connect(m_orderList, &OrderListWindow::backRequested,
            this, [this] { m_mainWindow->renderPrimaryPage(MainWindow::PrimaryPage::Profile); });
    connect(m_orderList, &OrderListWindow::refreshRequested,
            this, [this] { m_orderList->render(m_orderListState); });
    connect(m_orderDetail, &OrderDetailWindow::backRequested,
            this, [this] {
        m_orderList->render(m_orderListState);
        m_mainWindow->renderSecondaryPage(m_orderList);
    });
    connect(m_orderDetail, &OrderDetailWindow::actionRequested,
            m_orderList, &OrderListWindow::orderActionRequested);
    connect(m_orderList, &OrderListWindow::orderActionRequested,
            this, [this](const QString &businessId, OrderBusinessType type,
                         OrderListAction action) {
        for (const OrderListItemView &order : m_orderListState.orders) {
            if (order.businessId != businessId || order.type != type) continue;
            if (action == OrderListAction::ContinuePayment
                && type == OrderBusinessType::Charging) {
                m_settlementState.orderId = order.businessId;
                m_settlementState.stationName = order.stationName;
                m_settlementState.chargerCode = order.chargerCode;
                m_settlementState.durationText = order.durationText;
                m_settlementState.chargingTimeText = order.durationText;
                m_settlementState.energyText = order.energyText;
                m_settlementState.paymentMethodText = tr("钱包支付");
                m_settlementState.chargerInfoText = tr("%1 · %2号桩")
                                                        .arg(order.stationName,
                                                             order.chargerCode);
                m_settlementState.payableText = order.amountText;
                m_settlementState.canPay = true;
                m_settlement->render(m_settlementState);
                m_mainWindow->renderSecondaryPage(m_settlement);
            } else if (action == OrderListAction::ViewCharging) {
                renderChargingSessions(order.relatedBusinessId.isEmpty()
                                           ? businessId : order.relatedBusinessId);
                m_mainWindow->renderPrimaryPage(MainWindow::PrimaryPage::Charging);
            } else if (action == OrderListAction::StartReservedCharging) {
                if (QMessageBox::question(
                        m_orderList, tr("前往扫码充电"),
                        tr("该充电桩已预约，是否现在前往扫码充电？"),
                        QMessageBox::Yes | QMessageBox::No,
                        QMessageBox::Yes) == QMessageBox::Yes) {
                    m_scannerOpenedFromOrders = true;
                    m_scannerOpenedFromCharging = true;
                    m_scanState.expectedStationId = order.stationId;
                    m_scanState.expectedChargerId = order.chargerId;
                    m_scanState.chargerDisplayText = tr("已预约充电桩 %1")
                        .arg(order.chargerCode);
                    m_scanState.status = m_qrScanner->cameraAvailable()
                        ? ScanStatus::RequestingPermission : ScanStatus::Error;
                    m_scanState.cameraAvailable = m_qrScanner->cameraAvailable();
                    m_scanState.cameraPermissionGranted = false;
                    m_scanState.canRetry = false;
                    m_scanState.canImportImage = true;
                    m_scanState.message = m_scanState.cameraAvailable
                        ? tr("点击允许摄像头后开始实时扫码")
                        : tr("Demo 没有可用摄像头，可从相册选择二维码进行流程测试");
                    m_qrScanner->render(m_scanState);
                    m_mainWindow->renderSecondaryPage(m_qrScanner);
                }
            } else {
                OrderDetailViewState detail;
                detail.businessId = order.businessId;
                detail.relatedBusinessId = order.relatedBusinessId;
                detail.stationId = order.stationId;
                detail.chargerId = order.chargerId;
                detail.type = order.type;
                detail.titleText = order.type == OrderBusinessType::Charging
                                       ? tr("充电订单") : tr("预约订单");
                detail.stationName = order.stationName;
                detail.chargerCode = order.chargerCode;
                detail.createdAtText = order.createdAtText;
                detail.durationText = order.durationText;
                detail.energyText = order.energyText;
                detail.amountText = order.amountText;
                detail.paymentMethodText = tr("钱包支付");
                detail.statusText = order.statusText;
                detail.statusTone = order.statusTone;
                detail.message = order.summaryText;
                if (order.action != OrderListAction::ViewDetails) {
                    detail.action = order.action;
                    detail.actionText = order.actionText;
                    detail.actionEnabled = order.action != OrderListAction::None;
                }
                m_orderDetail->render(detail);
                m_mainWindow->renderSecondaryPage(m_orderDetail);
            }
            return;
        }
    });
    connect(m_mainWindow, &MainWindow::activeReservationRequested,
            m_stationDetail, &StationDetailWindow::activeReservationRequested);
    connect(m_walletRecharge, &WalletRechargeWindow::rechargeRequested,
            this, [this](const QString &amountText) {
        bool amountOk = false;
        const double amount = amountText.toDouble(&amountOk);
        QString balanceNumber = m_paymentBalanceText;
        balanceNumber.remove(QChar(0x00A5));
        bool balanceOk = false;
        const double balance = balanceNumber.toDouble(&balanceOk);
        if (!amountOk || amount <= 0.0) {
            QMessageBox::warning(m_walletRecharge, tr("充值金额无效"),
                                 tr("请输入大于 0 的充值金额。"));
            return;
        }
        const double updatedBalance = (balanceOk ? balance : 0.0) + amount;
        m_paymentBalanceCents = qRound64(updatedBalance * 100.0);
        m_paymentBalanceText = QStringLiteral("¥%1").arg(updatedBalance, 0, 'f', 2);
        m_walletRecharge->renderBalance(m_paymentBalanceText);
        QMessageBox::information(m_walletRecharge, tr("充值成功"),
                                 tr("钱包余额已更新，可返回继续支付。"));
    });
    connect(m_walletRecharge, &WalletRechargeWindow::backRequested,
            this, [this] {
        if (m_walletOpenedFromPayment) {
            m_walletOpenedFromPayment = false;
            m_paymentState.balanceText = m_paymentBalanceText;
            m_paymentState.balanceAfterPaymentText =
                balanceAfterPaymentText(m_paymentBalanceCents,
                                        m_paymentState.amountText);
            const qint64 amountCents = moneyTextToCents(m_paymentState.amountText);
            m_paymentState.canPay = !m_paymentOperationActive && amountCents >= 0
                                    && m_paymentBalanceCents >= amountCents;
            if (m_paymentState.canPay) {
                m_paymentState.status = PaymentViewStatus::Ready;
                m_paymentState.message.clear();
            }
            m_payment->render(m_paymentState);
            m_mainWindow->renderSecondaryPage(m_payment);
            return;
        }
        if (m_walletOpenedFromConfirmation) {
            m_mainWindow->renderSecondaryPage(m_chargeConfirmation);
            return;
        }
        m_mainWindow->renderPrimaryPage(MainWindow::PrimaryPage::Profile);
    });
}

UserDemoController::~UserDemoController()
{
    delete m_passwordChange;
}

void UserDemoController::showInitialPage()
{
    m_login->render(m_binder->currentLoginViewState());
    m_profileEdit->render(m_binder->currentProfileEditViewState());
    m_mainWindow->renderProfile(m_binder->currentProfileViewState());
    showOnly(m_login);
}

void UserDemoController::configureLogin(const QString &phone)
{
    m_currentAccountKey = phone;
    MockUserNetworkApi::Behavior loginBehavior;
    loginBehavior.delayMs = DemoDelayMs;
    const DemoUserData *user = m_demoUsers.contains(phone) ? &m_demoUsers[phone] : nullptr;
    if (user && user->failFirstLogin
        && !m_failedOnce.contains(phone)) {
        m_failedOnce.insert(phone);
        loginBehavior.outcome = MockUserNetworkApi::Outcome::Failure;
        loginBehavior.error.code = user->firstLoginFailureCode;
        loginBehavior.error.displayMessage = user->firstLoginFailureMessage;
        loginBehavior.error.retryable = user->firstLoginFailureRetryable;
    }
    m_network->setLoginBehavior(loginBehavior);

    const LoginResult result = makeLoginResult(phone, user, m_newUserNicknamePattern);
    m_network->setLoginResult(result);
    m_network->setUserProfileResult(makeProfileResult(result));
}

void UserDemoController::configureNicknameSave(const QString &nickname)
{
    MockUserNetworkApi::Behavior behavior;
    behavior.delayMs = DemoDelayMs;
    QFile file(QStringLiteral(":/demo/user-demo-data.tmp"));
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonObject failures = QJsonDocument::fromJson(file.readAll()).object()
                                          .value(QStringLiteral("nicknameFailures")).toObject();
        const QJsonObject failure = failures.value(nickname).toObject();
        if (!failure.isEmpty()) {
            if (failure.value(QStringLiteral("resultUnknown")).toBool()) {
                behavior.outcome = MockUserNetworkApi::Outcome::ResultUnknown;
            } else {
                behavior.outcome = MockUserNetworkApi::Outcome::Failure;
                behavior.error.code = failure.value(QStringLiteral("code")).toString();
                behavior.error.displayMessage = failure.value(QStringLiteral("message")).toString();
                behavior.error.retryable = failure.value(QStringLiteral("retryable")).toBool();
            }
        }
    }
    m_network->setNicknameBehavior(behavior);
}

void UserDemoController::rememberConfirmedLogin(const LoginResult &result)
{
    const QString phone = result.session.profile.phone;
    if (phone.isEmpty())
        return;

    DemoUserData &user = m_demoUsers[phone];
    user.nickname = result.session.profile.nickname;
    user.status = result.session.accountStatus;
}

void UserDemoController::rememberConfirmedNickname(const UserProfileResult &result)
{
    const QString phone = result.profile.phone;
    if (phone.isEmpty())
        return;

    DemoUserData &user = m_demoUsers[phone];
    user.nickname = result.profile.nickname;
    user.status = result.accountStatus;

    // Subsequent profile refreshes must return the same confirmed nickname.
    // Failed/result-unknown callbacks never reach this slot and are not saved.
    m_network->setUserProfileResult(result);
}

void UserDemoController::rememberConfirmedAvatar(const UserProfileResult &result)
{
    if (m_currentAccountKey.isEmpty())
        return;
    m_demoUsers[m_currentAccountKey].avatarDataUri = result.profile.avatarKey;
}

void UserDemoController::handleNavigation(NavigationTarget target)
{
    switch (target) {
    case NavigationTarget::Login:
        m_login->render(m_binder->currentLoginViewState());
        showOnly(m_login);
        break;
    case NavigationTarget::ProfileEdit:
        m_profileEditOpenedFromMain = false;
        m_profileEdit->setEditMode(ProfileEditMode::PhoneFirstSetup);
        m_profileEdit->render(m_binder->currentProfileEditViewState());
        showOnly(m_profileEdit);
        break;
    case NavigationTarget::Home:
    case NavigationTarget::RestrictedHome:
        m_mainWindow->renderProfile(m_binder->currentProfileViewState());
        m_mainWindow->renderPrimaryPage(MainWindow::PrimaryPage::Home);
        showOnly(m_mainWindow);
        m_mapBinder->activateHome();
        break;
    }
}

void UserDemoController::handleMapPage(MapPageTarget target,
                                      const QString &stationId)
{
    Q_UNUSED(stationId)
    switch (target) {
    case MapPageTarget::Home:
        m_mainWindow->renderPrimaryPage(MainWindow::PrimaryPage::Home);
        break;
    case MapPageTarget::StationDetail:
        renderStationDetailWithReservation(m_mapBinder->currentStationDetailState());
        m_mainWindow->renderSecondaryPage(m_stationDetail);
        break;
    case MapPageTarget::Navigation:
        m_navigation->render(m_mapBinder->currentNavigationState());
        m_mainWindow->renderSecondaryPage(m_navigation);
        break;
    }
}

void UserDemoController::renderStationDetailWithReservation(StationDetailViewState state)
{
    // Demo 端模拟后端的“一个账号仅一个有效预约”约束。Binder 刷新或选择
    // 其他电桩时，把已确认预约重新合并进最新详情，同时保留新的选中项，
    // 因此扫码充电仍可继续使用。
    if (m_reservedDetailState.activeReservation.has_value()) {
        state.activeReservation = m_reservedDetailState.activeReservation;
        const ActiveReservationView &active = *state.activeReservation;
        if (active.stationId == state.stationId) {
            for (ChargerListItemView &charger : state.chargers) {
                if (charger.chargerId != active.chargerId) continue;
                charger.statusText = QStringLiteral("已预约");
                charger.canCharge = false;
                charger.disabledReason = QStringLiteral("该充电桩已由当前用户预约");
                break;
            }
            if (!m_pendingReservationFocusChargerId.isEmpty()
                && active.chargerId == m_pendingReservationFocusChargerId) {
                state.selectedChargerId = active.chargerId;
                state.canContinueToConfirmation = true;
                m_pendingReservationFocusChargerId.clear();
            }
        }
        m_reservedDetailState = state;
    } else if (m_reservationCancellationLockedAccounts.contains(m_currentAccountKey)) {
        state.canCreateReservation = false;
        state.reservationDisabledReason =
            tr("刚刚取消过预约，冷却结束后可再次预约");
        m_reservedDetailState = state;
    }
    for (ChargerListItemView &charger : state.chargers) {
        const QString key = state.stationId + QLatin1Char('\n') + charger.chargerId;
        if (!m_demoChargingChargerKeys.contains(key)) continue;
        charger.statusText = tr("充电中");
        charger.canCharge = false;
        charger.disabledReason = tr("该充电桩正在为当前账号充电");
        if (state.selectedChargerId == charger.chargerId) {
            state.selectedChargerId.clear();
            state.canContinueToConfirmation = false;
        }
    }
    m_reservedDetailState = state;
    m_stationDetail->render(state);
    renderHomeWithReservation(m_mapBinder->currentHomeState());
}

void UserDemoController::renderHomeWithReservation(HomeMapViewState state)
{
    if (m_reservedDetailState.activeReservation.has_value())
        state.activeReservation = m_reservedDetailState.activeReservation;
    else
        state.activeReservation.reset();
    m_mainWindow->renderHome(state);
}

void UserDemoController::showChargeConfirmation(const QString &stationId,
                                                const QString &chargerId)
{
    if (m_scannerOpenedFromCharging && !m_chargingSessionDemoTemplates.isEmpty()) {
        m_chargeConfirmationState.stationId = stationId;
        m_chargeConfirmationState.chargerId = chargerId;
        for (const StationDetail &station : m_demoStations) {
            if (station.stationId != stationId) continue;
            m_chargeConfirmationState.stationName = station.summary.name;
            m_chargeConfirmationState.stationAddress = station.summary.address;
            if (station.summary.priceCentsPerKwh) {
                m_chargeConfirmationState.energyPriceText = QStringLiteral("¥%1/度")
                    .arg(*station.summary.priceCentsPerKwh / 100.0, 0, 'f', 2);
            }
            for (const ChargerSummary &charger : station.chargers) {
                if (charger.chargerId != chargerId) continue;
                m_chargeConfirmationState.chargerCode = charger.chargerId.section(
                    QLatin1Char('-'), -1).toUpper();
                m_chargeConfirmationState.chargerTypeText = charger.type;
                m_chargeConfirmationState.powerText = charger.powerKw
                    ? QStringLiteral("%1 kW").arg(*charger.powerKw, 0, 'f', 0)
                    : QStringLiteral("-- kW");
                break;
            }
            break;
        }
        m_chargeConfirmationState.chargerStatusText = tr("空闲");
        m_chargeConfirmationState.status = ChargeConfirmationStatus::Ready;
        m_chargeConfirmationState.canStart = true;
        m_chargeConfirmationState.message.clear();
        m_chargeConfirmation->render(m_chargeConfirmationState);
        m_mainWindow->renderSecondaryPage(m_chargeConfirmation);
        return;
    }
    StationDetailViewState detail = m_mapBinder->currentStationDetailState();
    if (m_reservedDetailState.activeReservation.has_value())
        detail = m_reservedDetailState;
    m_chargeConfirmationState.stationId = stationId;
    m_chargeConfirmationState.chargerId = chargerId;
    m_chargeConfirmationState.stationName = detail.name;
    m_chargeConfirmationState.stationAddress = detail.address;
    m_chargeConfirmationState.energyPriceText = detail.priceText;
    for (const ChargerListItemView &charger : detail.chargers) {
        if (charger.chargerId != chargerId) continue;
        m_chargeConfirmationState.chargerCode = charger.chargerId.section(
            QLatin1Char('-'), -1).toUpper();
        m_chargeConfirmationState.chargerTypeText = charger.title;
        m_chargeConfirmationState.powerText = charger.powerText;
        m_chargeConfirmationState.chargerStatusText = charger.statusText;
        break;
    }
    m_chargeConfirmationState.status = ChargeConfirmationStatus::Ready;
    m_chargeConfirmationState.canStart = true;
    m_chargeConfirmationState.message.clear();
    m_chargeConfirmation->render(m_chargeConfirmationState);
    m_mainWindow->renderSecondaryPage(m_chargeConfirmation);
}

void UserDemoController::renderChargingSessions(const QString &selectedOrderId)
{
    ChargingSessionCollectionViewState collection;
    collection.selectedOrderId = selectedOrderId;
    if (collection.selectedOrderId.isEmpty() && !m_chargingSessionStates.isEmpty())
        collection.selectedOrderId = m_chargingSessionStates.first().orderId;
    m_selectedChargingOrderId = collection.selectedOrderId;
    for (const ChargingSessionViewState &item : m_chargingSessionStates) {
        ChargingSessionSummaryView summary;
        summary.orderId = item.orderId;
        summary.stationName = item.stationName;
        summary.chargerCode = item.chargerCode;
        summary.chargerTypeText = item.chargerTypeText;
        summary.ratedPowerText = item.ratedPowerText;
        summary.currentPowerText = item.currentPowerText;
        summary.durationText = item.durationText;
        summary.statusText = tr("充电中");
        summary.status = item.status;
        collection.sessions.append(summary);
    }
    m_chargingSession->renderSessions(collection);
    for (const ChargingSessionViewState &item : m_chargingSessionStates) {
        if (item.orderId == collection.selectedOrderId) {
            m_chargingSession->render(item);
            return;
        }
    }
    m_chargingSession->render(ChargingSessionViewState{});
}

void UserDemoController::showOnly(QWidget *target)
{
    m_login->setVisible(target == m_login);
    m_profileEdit->setVisible(target == m_profileEdit);
    m_passwordChange->setVisible(target == m_passwordChange);
    m_mainWindow->setVisible(target == m_mainWindow);
    if (target) {
        target->raise();
        target->activateWindow();
    }
}
