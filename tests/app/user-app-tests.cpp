#include <QtTest>

#include "app/application.h"
#include "app/iuseruibinder.h"
#include "flow/iappflowcoordinator.h"
#include "modules/user/iuserservice.h"
#include "modules/user/mockusernetworkapi.h"

namespace
{

LoginResult loginResult(const QString &phone, bool isNewUser = false)
{
    LoginResult result;
    result.isNewUser = isNewUser;
    result.profileCompleted = !isNewUser;
    result.session.authenticated = true;
    result.session.accountStatus = AccountStatus::Normal;
    result.session.profile.userId = QStringLiteral("U") + phone;
    result.session.profile.phone = phone;
    result.session.profile.nickname = isNewUser
                                          ? QString()
                                          : QStringLiteral("Existing user");
    result.session.profile.balanceCents = 12345;
    return result;
}

UserProfileResult profileResult(const QString &phone,
                                const QString &nickname = QStringLiteral("Existing user"))
{
    UserProfileResult result;
    result.profile.userId = QStringLiteral("U") + phone;
    result.profile.phone = phone;
    result.profile.nickname = nickname;
    result.profile.balanceCents = 12345;
    result.accountStatus = AccountStatus::Normal;
    return result;
}

struct Fixture
{
    MockUserNetworkApi network;
    UserApplicationAssembly assembly{&network};

    IUserUiBinder *binder() const
    {
        return assembly.userUiBinder();
    }

    void configureLogin(const QString &phone, bool isNewUser = false)
    {
        network.setLoginResult(loginResult(phone, isNewUser));
        network.setUserProfileResult(profileResult(phone));
    }
};

} // namespace

class UserAppTests final : public QObject
{
    Q_OBJECT

private slots:
    void assemblyCreatesUiIndependentObjectGraph();
    void invalidPhoneBecomesValidationStateAndKeepsInput();
    void oldUserLoginPublishesHomeNavigation();
    void newUserLoginPublishesProfileState();
    void invalidNicknameBecomesValidationStateAndKeepsDraft();
    void unknownNicknameResultDisablesResubmission();
    void retryUsesCoordinatorPolicy();
    void profileSummaryTracksAuthenticatedSession();
    void logoutClearsPageStateAndNavigatesToLogin();
};

void UserAppTests::assemblyCreatesUiIndependentObjectGraph()
{
    Fixture fixture;

    QVERIFY(fixture.assembly.userService());
    QVERIFY(fixture.assembly.flowCoordinator());
    QVERIFY(fixture.assembly.userUiBinder());
    QCOMPARE(fixture.assembly.userService()->parent(), &fixture.assembly);
    QCOMPARE(fixture.assembly.flowCoordinator()->parent(), &fixture.assembly);
    QCOMPARE(fixture.assembly.userUiBinder()->parent(), &fixture.assembly);
}

void UserAppTests::invalidPhoneBecomesValidationStateAndKeepsInput()
{
    Fixture fixture;
    const QString input = QStringLiteral("123");

    fixture.binder()->loginRequested(input);

    const LoginViewState state = fixture.binder()->currentLoginViewState();
    QCOMPARE(state.submitState, SubmitState::ValidationError);
    QCOMPARE(state.phoneInput, input);
    QVERIFY(state.canSubmit);
    QCOMPARE(fixture.network.loginRequestCount(), 0);
}

void UserAppTests::oldUserLoginPublishesHomeNavigation()
{
    Fixture fixture;
    const QString phone = QStringLiteral("13800000000");
    fixture.configureLogin(phone);
    QSignalSpy navigationSpy(fixture.binder(), &IUserUiBinder::navigationRequested);

    fixture.binder()->loginRequested(phone);
    QCOMPARE(fixture.binder()->currentLoginViewState().submitState,
             SubmitState::Loading);

    QTRY_COMPARE(fixture.assembly.flowCoordinator()->currentFlow().state,
                 UserFlowState::Ready);
    QCOMPARE(fixture.binder()->currentLoginViewState().submitState,
             SubmitState::Success);
    QCOMPARE(navigationSpy.last().at(0).value<NavigationTarget>(),
             NavigationTarget::Home);
}

void UserAppTests::newUserLoginPublishesProfileState()
{
    Fixture fixture;
    const QString phone = QStringLiteral("13900001234");
    fixture.configureLogin(phone, true);
    QSignalSpy navigationSpy(fixture.binder(), &IUserUiBinder::navigationRequested);

    fixture.binder()->loginRequested(phone);

    QTRY_COMPARE(fixture.assembly.flowCoordinator()->currentFlow().state,
                 UserFlowState::ProfileRequired);
    const ProfileEditViewState state =
        fixture.binder()->currentProfileEditViewState();
    QCOMPARE(state.submitState, SubmitState::Idle);
    QCOMPARE(state.phone, phone);
    QCOMPARE(state.nicknameInput, QStringLiteral("用户1234"));
    QVERIFY(state.canSubmit);
    QCOMPARE(navigationSpy.last().at(0).value<NavigationTarget>(),
             NavigationTarget::ProfileEdit);
}

void UserAppTests::invalidNicknameBecomesValidationStateAndKeepsDraft()
{
    Fixture fixture;
    const QString phone = QStringLiteral("13900001234");
    fixture.configureLogin(phone, true);
    fixture.binder()->loginRequested(phone);
    QTRY_COMPARE(fixture.assembly.flowCoordinator()->currentFlow().state,
                 UserFlowState::ProfileRequired);

    const QString invalidNickname = QStringLiteral("   ");
    fixture.binder()->profileSaveRequested(invalidNickname);

    const ProfileEditViewState state =
        fixture.binder()->currentProfileEditViewState();
    QCOMPARE(state.submitState, SubmitState::ValidationError);
    QCOMPARE(state.nicknameInput, invalidNickname);
    QVERIFY(state.canSubmit);
}

void UserAppTests::unknownNicknameResultDisablesResubmission()
{
    Fixture fixture;
    const QString phone = QStringLiteral("13900001234");
    fixture.configureLogin(phone, true);
    fixture.binder()->loginRequested(phone);
    QTRY_COMPARE(fixture.assembly.flowCoordinator()->currentFlow().state,
                 UserFlowState::ProfileRequired);

    MockUserNetworkApi::Behavior unknown;
    unknown.outcome = MockUserNetworkApi::Outcome::ResultUnknown;
    fixture.network.setNicknameBehavior(unknown);
    MockUserNetworkApi::Behavior delayedQuery;
    delayedQuery.delayMs = 5000;
    fixture.network.setQueryBehavior(delayedQuery);

    fixture.binder()->profileSaveRequested(QStringLiteral("New nickname"));

    QTRY_COMPARE(fixture.assembly.flowCoordinator()->currentFlow().state,
                 UserFlowState::RecoveringProfileUpdate);
    const ProfileEditViewState state =
        fixture.binder()->currentProfileEditViewState();
    QCOMPARE(state.submitState, SubmitState::ResultUnknown);
    QVERIFY(!state.canSubmit);
}

void UserAppTests::retryUsesCoordinatorPolicy()
{
    Fixture fixture;
    const QString phone = QStringLiteral("13800000000");
    fixture.configureLogin(phone);
    MockUserNetworkApi::Behavior failure;
    failure.outcome = MockUserNetworkApi::Outcome::Failure;
    failure.error.code = QStringLiteral("request-timeout");
    failure.error.displayMessage = QStringLiteral("Request timed out.");
    failure.error.retryable = true;
    fixture.network.setLoginBehavior(failure);

    fixture.binder()->loginRequested(phone);
    QTRY_COMPARE(fixture.binder()->currentLoginViewState().submitState,
                 SubmitState::NetworkError);
    QVERIFY(fixture.binder()->currentLoginViewState().canRetry);

    fixture.network.setLoginBehavior(MockUserNetworkApi::Behavior{});
    fixture.binder()->retryRequested();

    QTRY_COMPARE(fixture.assembly.flowCoordinator()->currentFlow().state,
                 UserFlowState::Ready);
    QCOMPARE(fixture.network.loginRequestCount(), 2);
}

void UserAppTests::profileSummaryTracksAuthenticatedSession()
{
    Fixture fixture;
    const QString phone = QStringLiteral("13800000000");
    fixture.configureLogin(phone);

    fixture.binder()->loginRequested(phone);
    QTRY_COMPARE(fixture.assembly.flowCoordinator()->currentFlow().state,
                 UserFlowState::Ready);

    const ProfileViewState state = fixture.binder()->currentProfileViewState();
    QCOMPARE(state.nickname, QStringLiteral("Existing user"));
    QCOMPARE(state.maskedPhone, QStringLiteral("138****0000"));
    QCOMPARE(state.balanceText, QStringLiteral("¥123.45"));
    QCOMPARE(state.accountState, AccountDisplayState::Normal);
    QVERIFY(state.accountMessage.isEmpty());
}

void UserAppTests::logoutClearsPageStateAndNavigatesToLogin()
{
    Fixture fixture;
    const QString phone = QStringLiteral("13800000000");
    fixture.configureLogin(phone);
    fixture.binder()->loginRequested(phone);
    QTRY_COMPARE(fixture.assembly.flowCoordinator()->currentFlow().state,
                 UserFlowState::Ready);
    QSignalSpy navigationSpy(fixture.binder(), &IUserUiBinder::navigationRequested);

    fixture.binder()->logoutRequested();

    QCOMPARE(fixture.assembly.flowCoordinator()->currentFlow().state,
             UserFlowState::SignedOut);
    QCOMPARE(fixture.binder()->currentLoginViewState().submitState,
             SubmitState::Idle);
    QVERIFY(fixture.binder()->currentLoginViewState().phoneInput.isEmpty());
    QVERIFY(fixture.binder()->currentProfileEditViewState().phone.isEmpty());
    QCOMPARE(navigationSpy.last().at(0).value<NavigationTarget>(),
             NavigationTarget::Login);
}

QTEST_GUILESS_MAIN(UserAppTests)

#include "user-app-tests.moc"
