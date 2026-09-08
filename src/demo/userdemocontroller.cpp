#include "demo/userdemocontroller.h"
#include "demo/chargedemofixtureloader.h"
#include "demo/reservationdemofixtureloader.h"

#include "app/iuseruibinder.h"
#include "app/imapuibinder.h"
#include "modules/user/mockusernetworkapi.h"
#include "presentation/pages/auth/loginwindow.h"
#include "presentation/pages/shell/mainwindow.h"
#include "presentation/pages/profile/profileeditwindow.h"
#include "presentation/pages/home/navigationwindow.h"
#include "presentation/pages/home/stationdetailwindow.h"
#include "presentation/pages/profile/walletrechargewindow.h"
#include "presentation/pages/charging/chargeconfirmationwindow.h"
#include "presentation/pages/charging/reservationconfirmationwindow.h"
#include "presentation/pages/charging/qrcodescannerwindow.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QTimer>
#include <QMessageBox>

namespace
{

constexpr int DemoDelayMs = 450;

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
                                       LoginWindow *login,
                                       ProfileEditWindow *profileEdit,
                                       MainWindow *mainWindow,
                                       QObject *parent)
    : QObject(parent)
    , m_network(network)
    , m_binder(binder)
    , m_mapBinder(mapBinder)
    , m_login(login)
    , m_profileEdit(profileEdit)
    , m_mainWindow(mainWindow)
    , m_stationDetail(new StationDetailWindow(mainWindow))
    , m_navigation(new NavigationWindow(mainWindow))
    , m_walletRecharge(new WalletRechargeWindow(mainWindow))
    , m_chargeConfirmation(new ChargeConfirmationWindow(mainWindow))
    , m_reservationConfirmation(new ReservationConfirmationWindow(mainWindow))
    , m_qrScanner(new QrCodeScannerWindow(mainWindow))
    , m_demoUsers(loadDemoUsers(&m_newUserNicknamePattern))
{
    Q_ASSERT(m_network);
    Q_ASSERT(m_binder);
    Q_ASSERT(m_mapBinder);
    Q_ASSERT(m_login);
    Q_ASSERT(m_profileEdit);
    Q_ASSERT(m_mainWindow);

    // These widgets are constructed with MainWindow as their parent. Register
    // them before MainWindow is ever shown, otherwise Qt auto-shows ordinary
    // child widgets and the last-created recharge page covers the home page.
    m_mainWindow->registerSecondaryPage(m_stationDetail);
    m_mainWindow->registerSecondaryPage(m_navigation);
    m_mainWindow->registerSecondaryPage(m_walletRecharge);
    m_mainWindow->registerSecondaryPage(m_chargeConfirmation);
    m_mainWindow->registerSecondaryPage(m_reservationConfirmation);
    m_mainWindow->registerSecondaryPage(m_qrScanner);

    QString chargeFixtureError;
    if (!loadChargeConfirmationDemo(QStringLiteral(":/demo/charge-demo-data.tmp"),
                                    &m_chargeConfirmationState,
                                    &chargeFixtureError)) {
        m_chargeConfirmationState.status = ChargeConfirmationStatus::Error;
        m_chargeConfirmationState.message = chargeFixtureError;
        m_chargeConfirmationState.canRetry = true;
    }

    ReservationDemoFixture reservationFixture;
    QString reservationFixtureError;
    if (loadReservationDemoFixture(QStringLiteral(":/demo/reservation-demo-data.tmp"),
                                   &reservationFixture, &reservationFixtureError)) {
        m_reservationState.depositText = reservationFixture.depositText;
        m_reservationState.durationText = reservationFixture.durationText;
        m_reservationState.depositPolicyText = reservationFixture.depositPolicyText;
        m_reservationState.durationSeconds = reservationFixture.durationSeconds;
        m_reservationResponseDelayMs = reservationFixture.responseDelayMs;
        m_reservationCancellationCooldownSeconds =
            reservationFixture.cancellationCooldownSeconds;
        m_cancellationResponseDelayMs = reservationFixture.cancellationResponseDelayMs;
        m_cancellationOutcome = reservationFixture.cancellationOutcome;
        m_cancellationRetryOutcome = reservationFixture.cancellationRetryOutcome;
        m_cancellationFailureMessage = reservationFixture.cancellationFailureMessage;
        m_cancellationUnknownMessage = reservationFixture.cancellationUnknownMessage;
        m_reservationOutcome = reservationFixture.outcome;
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
    connect(m_profileEdit, &ProfileEditWindow::profileSaveRequested,
            this, &UserDemoController::configureNicknameSave);
    connect(m_profileEdit, &ProfileEditWindow::profileSaveRequested,
            m_binder, &IUserUiBinder::profileSaveRequested);
    connect(m_mainWindow, &MainWindow::logoutRequested,
            m_binder, &IUserUiBinder::logoutRequested);

    // The fixture file is only the initial server snapshot. Keep successful
    // registrations and profile updates in the Demo's runtime server state so
    // logout/relogin behaves like a real backend instead of replaying fixtures.
    connect(m_network, &IUserNetworkApi::loginSucceeded,
            this, &UserDemoController::rememberConfirmedLogin);
    connect(m_network, &IUserNetworkApi::nicknameUpdateSucceeded,
            this, &UserDemoController::rememberConfirmedNickname);

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
        m_profileEdit->render(m_binder->currentProfileEditViewState());
        showOnly(m_profileEdit);
    });
    connect(m_profileEdit, &ProfileEditWindow::backRequested,
            this, [this] {
        if (m_profileEditOpenedFromMain) {
            showOnly(m_mainWindow);
            return;
        }
        // 新用户资料尚未完善时不能绕过该步骤进入首页；返回即放弃本次
        // 已认证会话，由既有流程统一清理状态并导航回登录页。
        m_binder->logoutRequested();
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
        m_scanState.expectedStationId = stationId;
        m_scanState.expectedChargerId = chargerId;
        m_scanState.chargerDisplayText = tr("充电桩 %1").arg(
            chargerId.section(QLatin1Char('-'), -1).toUpper());
        m_scanState.status = ScanStatus::Error;
        m_scanState.cameraAvailable = false;
        m_scanState.cameraPermissionGranted = false;
        m_scanState.canRetry = false;
        m_scanState.canImportImage = true;
        m_scanState.message = tr("Demo 尚未接入摄像头，可从相册选择二维码进行流程测试");
        m_qrScanner->render(m_scanState);
        m_mainWindow->renderSecondaryPage(m_qrScanner);
    });
    connect(m_qrScanner, &QrCodeScannerWindow::backRequested,
            this, [this] { m_mainWindow->renderSecondaryPage(m_stationDetail); });
    connect(m_qrScanner, &QrCodeScannerWindow::imageImportRequested,
            this, [this] {
        showChargeConfirmation(m_scanState.expectedStationId,
                               m_scanState.expectedChargerId);
    });
    connect(m_qrScanner, &QrCodeScannerWindow::scanRetryRequested,
            this, [this] { m_qrScanner->render(m_scanState); });
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
            this, [this] { m_reservationConfirmation->render(m_reservationState); });
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
        ReservationConfirmationViewState submitting = m_reservationState;
        submitting.status = ReservationConfirmationStatus::Submitting;
        submitting.canReserve = false;
        submitting.message = QStringLiteral("正在提交预约…");
        m_reservationConfirmation->render(submitting);
        QTimer::singleShot(m_reservationResponseDelayMs, this,
                           [this, stationId, chargerId, durationSeconds] {
            if (m_reservationOutcome != QStringLiteral("success")) {
                m_reservationState.status = ReservationConfirmationStatus::Error;
                m_reservationState.canReserve = false;
                m_reservationState.canRetry = true;
                m_reservationState.message = QStringLiteral("预约失败，请重试");
                m_reservationConfirmation->render(m_reservationState);
                return;
            }

            m_reservedDetailState = m_mapBinder->currentStationDetailState();
            for (ChargerListItemView &charger : m_reservedDetailState.chargers) {
                if (charger.chargerId != chargerId) continue;
                charger.statusText = QStringLiteral("已预约");
                charger.canCharge = false;
                charger.disabledReason = QStringLiteral("该充电桩已由当前用户预约");
                break;
            }
            m_reservedDetailState.selectedChargerId = chargerId;
            m_reservedDetailState.canContinueToConfirmation = true;
            ActiveReservationView active;
            active.reservationId = QStringLiteral("demo-reservation-%1")
                                       .arg(QDateTime::currentMSecsSinceEpoch());
            active.stationId = stationId;
            active.chargerId = chargerId;
            active.expiresAtUtc = QDateTime::currentDateTimeUtc().addSecs(durationSeconds);
            active.canCancel = true;
            m_reservedDetailState.activeReservation = active;
            renderStationDetailWithReservation(m_reservedDetailState);
            m_mainWindow->renderSecondaryPage(m_stationDetail);
        });
    });
    connect(m_stationDetail, &StationDetailWindow::reservationExpiredRefreshRequested,
            this, [this] {
        if (!m_reservedDetailState.activeReservation.has_value()) return;
        const QString chargerId = m_reservedDetailState.activeReservation->chargerId;
        for (ChargerListItemView &charger : m_reservedDetailState.chargers) {
            if (charger.chargerId != chargerId) continue;
            charger.statusText = QStringLiteral("空闲");
            charger.canCharge = true;
            charger.disabledReason.clear();
            break;
        }
        m_reservedDetailState.activeReservation.reset();
        renderStationDetailWithReservation(m_reservedDetailState);
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
    const auto submitCancellation = [this](const QString &reservationId,
                                            bool needsConfirmation) {
        if (!m_reservedDetailState.activeReservation
            || m_reservedDetailState.activeReservation->reservationId != reservationId) {
            QMessageBox::warning(m_stationDetail, tr("无法取消"),
                                 tr("预约状态已经变化，请刷新后重试。"));
            return;
        }
        if (needsConfirmation
            && QMessageBox::question(
                   m_stationDetail, tr("取消预约"),
                   tr("确定取消当前预约吗？频繁预约和取消可能会被限制预约。"))
                   != QMessageBox::Yes)
            return;
        m_reservedDetailState.activeReservation->cancellationStatus =
            ReservationCancellationStatus::Submitting;
        m_reservedDetailState.activeReservation->cancellationMessage = tr("正在取消预约…");
        m_reservedDetailState.activeReservation->canCancel = false;
        m_reservedDetailState.activeReservation->canRetryCancel = false;
        renderStationDetailWithReservation(m_reservedDetailState);
        const QString outcome = needsConfirmation ? m_cancellationOutcome
                                                  : m_cancellationRetryOutcome;
        QTimer::singleShot(m_cancellationResponseDelayMs, this,
                           [this, reservationId, outcome] {
            if (!m_reservedDetailState.activeReservation
                || m_reservedDetailState.activeReservation->reservationId != reservationId)
                return;
            if (outcome == QStringLiteral("failure")) {
                ActiveReservationView &active = *m_reservedDetailState.activeReservation;
                active.cancellationStatus = ReservationCancellationStatus::Error;
                active.cancellationMessage = m_cancellationFailureMessage;
                active.canCancel = false;
                active.canRetryCancel = true;
                renderStationDetailWithReservation(m_reservedDetailState);
                QMessageBox::warning(m_stationDetail, tr("取消失败"),
                                     m_cancellationFailureMessage);
                return;
            }
            if (outcome == QStringLiteral("result_unknown")) {
                ActiveReservationView &active = *m_reservedDetailState.activeReservation;
                active.cancellationStatus = ReservationCancellationStatus::ResultUnknown;
                active.cancellationMessage = m_cancellationUnknownMessage;
                active.canCancel = false;
                active.canRetryCancel = false;
                renderStationDetailWithReservation(m_reservedDetailState);
                QMessageBox::warning(m_stationDetail, tr("取消结果待确认"),
                                     m_cancellationUnknownMessage);
                return;
            }
            const QString chargerId = m_reservedDetailState.activeReservation->chargerId;
            for (ChargerListItemView &charger : m_reservedDetailState.chargers) {
                if (charger.chargerId != chargerId) continue;
                charger.statusText = QStringLiteral("空闲");
                charger.canCharge = true;
                charger.disabledReason.clear();
                break;
            }
            m_reservedDetailState.activeReservation.reset();
            m_reservedDetailState.selectedChargerId.clear();
            m_reservedDetailState.canContinueToConfirmation = false;
            m_reservedDetailState.canCreateReservation = false;
            m_reservedDetailState.reservationDisabledReason =
                tr("刚刚取消过预约，%1 秒后可再次预约")
                    .arg(m_reservationCancellationCooldownSeconds);
            const QString cancelledAccountKey = m_currentAccountKey;
            m_reservationCancellationLockedAccounts.insert(cancelledAccountKey);
            renderStationDetailWithReservation(m_reservedDetailState);
            QMessageBox::information(
                m_stationDetail, tr("预约已取消，进入冷却"),
                tr("预约已取消。为避免反复预约和取消，%1 秒后才可再次预约。")
                    .arg(m_reservationCancellationCooldownSeconds));
            QTimer::singleShot(m_reservationCancellationCooldownSeconds * 1000,
                               this, [this, cancelledAccountKey] {
                m_reservationCancellationLockedAccounts.remove(cancelledAccountKey);
                if (m_currentAccountKey != cancelledAccountKey) return;
                m_reservedDetailState.canCreateReservation = true;
                m_reservedDetailState.reservationDisabledReason.clear();
                renderStationDetailWithReservation(m_reservedDetailState);
            });
        });
    };
    connect(m_stationDetail, &StationDetailWindow::cancelReservationRequested,
            this, [submitCancellation](const QString &reservationId) {
        submitCancellation(reservationId, true);
    });
    connect(m_stationDetail, &StationDetailWindow::cancelReservationRetryRequested,
            this, [submitCancellation](const QString &reservationId) {
        submitCancellation(reservationId, false);
    });
    connect(m_chargeConfirmation, &ChargeConfirmationWindow::backRequested,
            this, [this] { m_mainWindow->renderSecondaryPage(m_stationDetail); });
    connect(m_chargeConfirmation, &ChargeConfirmationWindow::confirmationRefreshRequested,
            this, [this] { m_chargeConfirmation->render(m_chargeConfirmationState); });
    connect(m_chargeConfirmation, &ChargeConfirmationWindow::rechargeRequested,
            this, [this] {
        m_walletOpenedFromConfirmation = true;
        m_walletRecharge->renderBalance(m_chargeConfirmationState.walletBalanceText);
        m_mainWindow->renderSecondaryPage(m_walletRecharge);
    });
    connect(m_chargeConfirmation, &ChargeConfirmationWindow::startChargingRequested,
            this, [this](const QString &, const QString &) {
        ChargeConfirmationViewState submitting = m_chargeConfirmationState;
        submitting.status = ChargeConfirmationStatus::Submitting;
        submitting.canStart = false;
        m_chargeConfirmation->render(submitting);
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
    connect(m_mainWindow, &MainWindow::activeReservationRequested,
            m_stationDetail, &StationDetailWindow::activeReservationRequested);
    connect(m_walletRecharge, &WalletRechargeWindow::backRequested,
            this, [this] {
        if (m_walletOpenedFromConfirmation) {
            m_mainWindow->renderSecondaryPage(m_chargeConfirmation);
            return;
        }
        m_mainWindow->renderPrimaryPage(MainWindow::PrimaryPage::Profile);
    });
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

void UserDemoController::handleNavigation(NavigationTarget target)
{
    switch (target) {
    case NavigationTarget::Login:
        m_login->render(m_binder->currentLoginViewState());
        showOnly(m_login);
        break;
    case NavigationTarget::ProfileEdit:
        m_profileEditOpenedFromMain = false;
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

void UserDemoController::showOnly(QWidget *target)
{
    m_login->setVisible(target == m_login);
    m_profileEdit->setVisible(target == m_profileEdit);
    m_mainWindow->setVisible(target == m_mainWindow);
    if (target) {
        target->raise();
        target->activateWindow();
    }
}
