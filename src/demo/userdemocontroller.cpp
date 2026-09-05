#include "demo/userdemocontroller.h"

#include "app/iuseruibinder.h"
#include "modules/user/mockusernetworkapi.h"
#include "presentation/pages/loginwindow.h"
#include "presentation/pages/mainwindow.h"
#include "presentation/pages/profileeditwindow.h"

namespace
{

constexpr int DemoDelayMs = 450;

LoginResult makeLoginResult(const QString &phone, bool isNewUser,
                            AccountStatus accountStatus)
{
    LoginResult result;
    result.isNewUser = isNewUser;
    result.profileCompleted = !isNewUser;
    result.session.authenticated = true;
    result.session.accountStatus = accountStatus;
    result.session.profile.userId = QStringLiteral("U") + phone;
    result.session.profile.phone = phone;
    if (!isNewUser) {
        result.session.profile.nickname = accountStatus == AccountStatus::Frozen
                                              ? QStringLiteral("受限用户")
                                              : QStringLiteral("演示用户");
    }
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
    , m_registeredPhones{QStringLiteral("13800000000"),
                         QStringLiteral("13900000000"),
                         QStringLiteral("13600000000"),
                         QStringLiteral("13700000000")}
{
    Q_ASSERT(m_network);
    Q_ASSERT(m_binder);
    Q_ASSERT(m_login);
    Q_ASSERT(m_profileEdit);
    Q_ASSERT(m_mainWindow);

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
    if (phone == QStringLiteral("13700000000")
        && !m_failedOnce.contains(phone)) {
        m_failedOnce.insert(phone);
        loginBehavior.outcome = MockUserNetworkApi::Outcome::Failure;
        loginBehavior.error.code = QStringLiteral("request-timeout");
        loginBehavior.error.displayMessage =
            QStringLiteral("演示网络超时，请再次点击登录重试。");
        loginBehavior.error.retryable = true;
    }
    m_network->setLoginBehavior(loginBehavior);

    const bool isNewUser = !m_registeredPhones.contains(phone);
    AccountStatus status = AccountStatus::Normal;
    if (phone == QStringLiteral("13900000000")) {
        status = AccountStatus::Frozen;
    } else if (phone == QStringLiteral("13600000000")) {
        status = AccountStatus::Unknown;
    }

    const LoginResult result = makeLoginResult(phone, isNewUser, status);
    m_network->setLoginResult(result);
    m_network->setUserProfileResult(makeProfileResult(result));
    if (phone.size() == 11) {
        m_registeredPhones.insert(phone);
    }
}

void UserDemoController::configureNicknameSave(const QString &nickname)
{
    MockUserNetworkApi::Behavior behavior;
    behavior.delayMs = DemoDelayMs;
    if (nickname == QStringLiteral("网络错误")) {
        behavior.outcome = MockUserNetworkApi::Outcome::Failure;
        behavior.error.code = QStringLiteral("connection-lost");
        behavior.error.displayMessage = QStringLiteral("演示网络连接中断。");
        behavior.error.retryable = true;
    } else if (nickname == QStringLiteral("服务错误")) {
        behavior.outcome = MockUserNetworkApi::Outcome::Failure;
        behavior.error.code = QStringLiteral("server-demo-error");
        behavior.error.displayMessage = QStringLiteral("演示服务端拒绝保存昵称。");
    } else if (nickname == QStringLiteral("结果未知")) {
        behavior.outcome = MockUserNetworkApi::Outcome::ResultUnknown;
    }
    m_network->setNicknameBehavior(behavior);
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
