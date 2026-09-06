#include "demo/userdemocontroller.h"

#include "app/iuseruibinder.h"
#include "modules/user/mockusernetworkapi.h"
#include "presentation/pages/auth/loginwindow.h"
#include "presentation/pages/shell/mainwindow.h"
#include "presentation/pages/profile/profileeditwindow.h"
#include "presentation/pages/home/navigationwindow.h"
#include "presentation/pages/home/stationdetailwindow.h"
#include "presentation/pages/profile/walletrechargewindow.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

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
                                       LoginWindow *login,
                                       ProfileEditWindow *profileEdit,
                                       MainWindow *mainWindow,
                                       QObject *parent)
    : QObject(parent)
    , m_network(network)
    , m_binder(binder)
    , m_login(login)
    , m_profileEdit(profileEdit)
    , m_mainWindow(mainWindow)
    , m_stationDetail(new StationDetailWindow(mainWindow))
    , m_navigation(new NavigationWindow(mainWindow))
    , m_walletRecharge(new WalletRechargeWindow(mainWindow))
    , m_demoUsers(loadDemoUsers(&m_newUserNicknamePattern))
{
    Q_ASSERT(m_network);
    Q_ASSERT(m_binder);
    Q_ASSERT(m_login);
    Q_ASSERT(m_profileEdit);
    Q_ASSERT(m_mainWindow);

    // These widgets are constructed with MainWindow as their parent. Register
    // them before MainWindow is ever shown, otherwise Qt auto-shows ordinary
    // child widgets and the last-created recharge page covers the home page.
    m_mainWindow->registerSecondaryPage(m_stationDetail);
    m_mainWindow->registerSecondaryPage(m_navigation);
    m_mainWindow->registerSecondaryPage(m_walletRecharge);

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
        m_profileEdit->render(m_binder->currentProfileEditViewState());
        showOnly(m_profileEdit);
    });
    connect(m_profileEdit, &ProfileEditWindow::backRequested,
            this, [this] {
        // ProfileRequired 时禁止绕过资料完善；普通资料编辑完成后才允许返回。
        if (m_binder->currentProfileEditViewState().submitState
            == SubmitState::Success) {
            showOnly(m_mainWindow);
        }
    });

    connect(m_mainWindow, &MainWindow::stationDetailsRequested,
            this, [this](const QString &) {
        m_mainWindow->renderSecondaryPage(m_stationDetail);
    });
    connect(m_stationDetail, &StationDetailWindow::backRequested,
            this, [this] {
        m_mainWindow->renderPrimaryPage(MainWindow::PrimaryPage::Home);
    });
    connect(m_stationDetail, &StationDetailWindow::navigationRequested,
            this, [this] {
        m_mainWindow->renderSecondaryPage(m_navigation);
    });
    connect(m_navigation, &NavigationWindow::backRequested,
            this, [this] {
        m_mainWindow->renderSecondaryPage(m_stationDetail);
    });
    connect(m_mainWindow, &MainWindow::rechargePageRequested,
            this, [this] {
        m_walletRecharge->renderBalance(
            m_binder->currentProfileViewState().balanceText);
        m_mainWindow->renderSecondaryPage(m_walletRecharge);
    });
    connect(m_walletRecharge, &WalletRechargeWindow::backRequested,
            this, [this] {
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
        m_profileEdit->render(m_binder->currentProfileEditViewState());
        showOnly(m_profileEdit);
        break;
    case NavigationTarget::Home:
    case NavigationTarget::RestrictedHome:
        m_mainWindow->renderProfile(m_binder->currentProfileViewState());
        m_mainWindow->renderPrimaryPage(MainWindow::PrimaryPage::Home);
        showOnly(m_mainWindow);
        break;
    }
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
