#include "modules/user/mockusernetworkapi.h"
#include "modules/user/userservice.h"

#include <QSignalSpy>
#include <QtTest>

class UserModuleTests final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void rejectsInvalidPhoneWithoutNetworkRequest();
    void rejectsDuplicateLogin();
    void acceptsNormalUser();
    void acceptsNewAndFrozenUser();
    void nicknameUpdateKeepsSessionState();
    void propagatesRetryableNetworkFailure();
    void blocksRetryAfterUnknownProfileUpdate();
    void profileRefreshUnblocksUnknownResult();
    void ignoresLateLoginAfterLogout();
    void reLoginIsolatesPreviousSessionRequests();
    // —— 第一步规格新增：操作状态 ——
    void publishesRunningAndIdleForLogin();
    void duplicateLoginKeepsOriginalRunningState();
    void failedLoginReturnsOperationToIdle();
    void unknownNicknameUpdatePublishesResultUnknown();
    void failedNicknameUpdateReturnsOperationToIdle();
    void successfulProfileRefreshClearsResultUnknownState();
    void failedProfileRefreshKeepsResultUnknown();
    void profileRefreshDoesNotFinishRunningNicknameUpdate();
    void logoutClearsAllOperationStates();
    void logoutDoesNotPublishIdleForNeverStartedOperations();
    void lateResponseDoesNotChangeOperationState();
    void profileRefreshKeepsSessionAuthenticated();
    void rejectsProfileForDifferentUser();
    void reLoginClearsPreviousOperationContexts();
    // —— 第一步规格新增：昵称校验 ——
    void rejectsEmptyNickname();
    void rejectsNicknameLongerThanTwentyCharacters();
    void rejectsNicknameWithControlCharacters();
    void trimsNicknameBeforeSending();
    void changesPasswordThroughDedicatedOperation();
};

void UserModuleTests::initTestCase()
{
    qRegisterMetaType<ClientError>();
    qRegisterMetaType<LoginResult>();
    qRegisterMetaType<UserSession>();
    qRegisterMetaType<UserOperation>();
    qRegisterMetaType<UserOperationState>();
    qRegisterMetaType<UserOperationStatus>();
    qRegisterMetaType<OperationResult>();
}

void UserModuleTests::changesPasswordThroughDedicatedOperation()
{
    MockUserNetworkApi network;
    LoginResult login;
    login.session.profile.userId = QStringLiteral("alice");
    login.session.accountStatus = AccountStatus::Normal;
    network.setLoginResult(login);
    UserService service(&network);
    service.loginByCredentials(QStringLiteral("alice"), QStringLiteral("old123"));
    QTRY_VERIFY(service.currentSession().authenticated);

    QSignalSpy changed(&service, &IUserService::passwordChanged);
    service.changePassword(QStringLiteral("old123"), QStringLiteral("new12345"));
    QCOMPARE(service.operationStatus(UserOperation::ChangePassword).state,
             UserOperationState::Running);
    QTRY_COMPARE(changed.count(), 1);
    QCOMPARE(service.operationStatus(UserOperation::ChangePassword).state,
             UserOperationState::Idle);
}

void UserModuleTests::rejectsInvalidPhoneWithoutNetworkRequest()
{
    MockUserNetworkApi network;
    UserService service(&network);
    QSignalSpy errors(&service, &IUserService::operationFailed);

    service.loginByPhone(QStringLiteral("123"));

    QCOMPARE(network.loginRequestCount(), 0);
    QCOMPARE(errors.count(), 1);
    QCOMPARE(qvariant_cast<ClientError>(errors.takeFirst().at(0)).code,
             QStringLiteral("invalid-phone"));
}

void UserModuleTests::rejectsDuplicateLogin()
{
    MockUserNetworkApi network;
    MockUserNetworkApi::Behavior behavior;
    behavior.delayMs = 50;
    network.setLoginBehavior(behavior);
    UserService service(&network);
    QSignalSpy errors(&service, &IUserService::operationFailed);

    service.loginByPhone(QStringLiteral("13800138000"));
    service.loginByPhone(QStringLiteral("13800138000"));

    QCOMPARE(network.loginRequestCount(), 1);
    QCOMPARE(errors.count(), 1);
    QCOMPARE(qvariant_cast<ClientError>(errors.takeFirst().at(0)).code,
             QStringLiteral("request-in-progress"));
}

void UserModuleTests::acceptsNormalUser()
{
    MockUserNetworkApi network;
    LoginResult expected;
    expected.session.profile.userId = QStringLiteral("13800138000");
    expected.session.profile.phone = QStringLiteral("13800138000");
    expected.session.accountStatus = AccountStatus::Normal;
    network.setLoginResult(expected);
    UserService service(&network);
    QSignalSpy successes(&service, &IUserService::loginSucceeded);

    service.loginByPhone(QStringLiteral("13800138000"));

    QTRY_COMPARE(successes.count(), 1);
    const LoginResult actual = qvariant_cast<LoginResult>(successes.takeFirst().at(0));
    QVERIFY(actual.session.authenticated);
    QVERIFY(!actual.isNewUser);
    QCOMPARE(actual.session.accountStatus, AccountStatus::Normal);
}

void UserModuleTests::acceptsNewAndFrozenUser()
{
    MockUserNetworkApi network;
    LoginResult expected;
    expected.session.profile.userId = QStringLiteral("13800138000");
    expected.session.profile.phone = QStringLiteral("13800138000");
    expected.session.accountStatus = AccountStatus::Frozen;
    expected.isNewUser = true;
    network.setLoginResult(expected);
    UserService service(&network);
    QSignalSpy successes(&service, &IUserService::loginSucceeded);

    service.loginByPhone(QStringLiteral("13800138000"));

    QTRY_COMPARE(successes.count(), 1);
    const LoginResult actual = qvariant_cast<LoginResult>(successes.takeFirst().at(0));
    QVERIFY(actual.session.authenticated);
    QVERIFY(actual.isNewUser);
    QCOMPARE(actual.session.accountStatus, AccountStatus::Frozen);
}

/* 回归：219 应答只携带昵称（协议 v2.1），改昵称不得清空会话 phone
 * 或把 accountStatus 抹成 Unknown（修复前会覆盖为部分资料） */
void UserModuleTests::nicknameUpdateKeepsSessionState()
{
    MockUserNetworkApi network;
    LoginResult expected;
    expected.session.profile.userId = QStringLiteral("13800138000");
    expected.session.profile.phone = QStringLiteral("13800138000");
    expected.session.accountStatus = AccountStatus::Normal;
    network.setLoginResult(expected);

    UserProfileResult profileResult;
    profileResult.profile.userId = QStringLiteral("13800138000");
    network.setUserProfileResult(profileResult);

    UserService service(&network);
    QSignalSpy successes(&service, &IUserService::loginSucceeded);
    service.loginByPhone(QStringLiteral("13800138000"));
    QTRY_COMPARE(successes.count(), 1);

    service.updateNickname(QStringLiteral("Bob"));
    QTRY_COMPARE(service.currentSession().profile.nickname, QStringLiteral("Bob"));
    QVERIFY(service.currentSession().authenticated);
    QCOMPARE(service.currentSession().profile.phone, QStringLiteral("13800138000"));
    QCOMPARE(service.currentSession().accountStatus, AccountStatus::Normal);
}

void UserModuleTests::propagatesRetryableNetworkFailure()
{
    MockUserNetworkApi network;
    MockUserNetworkApi::Behavior timeout;
    timeout.outcome = MockUserNetworkApi::Outcome::Failure;
    timeout.error.code = QStringLiteral("network-timeout");
    timeout.error.displayMessage = QStringLiteral("Connection timed out.");
    timeout.error.retryable = true;
    network.setLoginBehavior(timeout);
    UserService service(&network);
    QSignalSpy errors(&service, &IUserService::operationFailed);

    service.loginByPhone(QStringLiteral("13800138000"));

    QTRY_COMPARE(errors.count(), 1);
    const ClientError error = qvariant_cast<ClientError>(errors.takeFirst().at(0));
    QCOMPARE(error.code, QStringLiteral("network-timeout"));
    QVERIFY(error.retryable);
    QVERIFY(!service.currentSession().authenticated);
}

void UserModuleTests::blocksRetryAfterUnknownProfileUpdate()
{
    MockUserNetworkApi network;
    LoginResult login;
    login.session.profile.userId = QStringLiteral("13800138000");
    login.session.profile.phone = QStringLiteral("13800138000");
    network.setLoginResult(login);
    UserService service(&network);
    QSignalSpy errors(&service, &IUserService::operationFailed);

    service.loginByPhone(QStringLiteral("13800138000"));
    QTRY_VERIFY(service.currentSession().authenticated);

    MockUserNetworkApi::Behavior unknown;
    unknown.outcome = MockUserNetworkApi::Outcome::ResultUnknown;
    network.setNicknameBehavior(unknown);
    service.updateNickname(QStringLiteral("Alice"));
    QTRY_COMPARE(errors.count(), 1);

    service.updateNickname(QStringLiteral("Bob"));
    QCOMPARE(network.nicknameRequestCount(), 1);
    QCOMPARE(errors.count(), 2);
    QCOMPARE(qvariant_cast<ClientError>(errors.takeLast().at(0)).code,
             QStringLiteral("result-unknown-pending"));
}

void UserModuleTests::ignoresLateLoginAfterLogout()
{
    MockUserNetworkApi network;
    MockUserNetworkApi::Behavior delayedLogin;
    delayedLogin.delayMs = 50;
    network.setLoginBehavior(delayedLogin);
    UserService service(&network);
    QSignalSpy successes(&service, &IUserService::loginSucceeded);

    service.loginByPhone(QStringLiteral("13800138000"));
    service.logout();

    QTest::qWait(80);
    QCOMPARE(successes.count(), 0);
    QVERIFY(!service.currentSession().authenticated);
}

void UserModuleTests::profileRefreshUnblocksUnknownResult()
{
    MockUserNetworkApi network;
    LoginResult login;
    login.session.profile.userId = QStringLiteral("13800138000");
    login.session.profile.phone = QStringLiteral("13800138000");
    network.setLoginResult(login);
    UserService service(&network);
    QSignalSpy errors(&service, &IUserService::operationFailed);

    service.loginByPhone(QStringLiteral("13800138000"));
    QTRY_VERIFY(service.currentSession().authenticated);

    // 改昵称结果未知 → 更新被锁定
    MockUserNetworkApi::Behavior unknown;
    unknown.outcome = MockUserNetworkApi::Outcome::ResultUnknown;
    network.setNicknameBehavior(unknown);
    service.updateNickname(QStringLiteral("Alice"));
    QTRY_COMPARE(errors.count(), 1);
    service.updateNickname(QStringLiteral("Bob"));
    QCOMPARE(network.nicknameRequestCount(), 1);
    QCOMPARE(errors.count(), 2);

    // 合同 §12.1 第 4 条：117 查询成功 = 服务端状态已知 → 解除锁定。
    // 本例查询证明昵称已生效为 Alice，用户可据此决定是否再改。
    UserProfileResult confirmed;
    confirmed.profile.userId = QStringLiteral("13800138000");
    confirmed.profile.phone = QStringLiteral("13800138000");
    confirmed.profile.nickname = QStringLiteral("Alice");
    confirmed.accountStatus = AccountStatus::Normal;
    network.setUserProfileResult(confirmed);
    service.refreshCurrentUser();
    QTRY_COMPARE(service.currentSession().profile.nickname, QStringLiteral("Alice"));

    MockUserNetworkApi::Behavior ok;
    network.setNicknameBehavior(ok);
    service.updateNickname(QStringLiteral("Bob"));
    QTRY_COMPARE(service.currentSession().profile.nickname, QStringLiteral("Bob"));
    QCOMPARE(network.nicknameRequestCount(), 2);
}

void UserModuleTests::reLoginIsolatesPreviousSessionRequests()
{
    MockUserNetworkApi network;
    LoginResult loginA;
    loginA.session.profile.userId = QStringLiteral("13800138000");
    loginA.session.profile.phone = QStringLiteral("13800138000");
    network.setLoginResult(loginA);
    UserService service(&network);
    QSignalSpy successes(&service, &IUserService::loginSucceeded);

    service.loginByPhone(QStringLiteral("13800138000"));
    QTRY_COMPARE(successes.count(), 1);

    // A 的资料查询在途（延迟应答）
    MockUserNetworkApi::Behavior slowQuery;
    slowQuery.delayMs = 50;
    network.setQueryBehavior(slowQuery);
    UserProfileResult staleProfile;
    staleProfile.profile.userId = QStringLiteral("13800138000");
    staleProfile.profile.phone = QStringLiteral("13800138000");
    staleProfile.profile.nickname = QStringLiteral("旧账号资料");
    network.setUserProfileResult(staleProfile);
    service.refreshCurrentUser();

    // 不登出直接换号登录 B（合同 §12.1 第 5 条）
    LoginResult loginB;
    loginB.session.profile.userId = QStringLiteral("13900139000");
    loginB.session.profile.phone = QStringLiteral("13900139000");
    network.setLoginResult(loginB);
    service.loginByPhone(QStringLiteral("13900139000"));
    QTRY_COMPARE(successes.count(), 2);
    QCOMPARE(service.currentSession().profile.userId, QStringLiteral("13900139000"));

    // A 的迟到 218 到达：不得覆盖 B 的会话
    QTest::qWait(80);
    QCOMPARE(service.currentSession().profile.userId, QStringLiteral("13900139000"));
    QVERIFY(service.currentSession().profile.nickname.isEmpty());
}

/* ===== 第一步规格新增：测试辅助 ===== */
namespace {

UserOperationStatus statusAt(const QSignalSpy &spy, int index)
{
    return qvariant_cast<UserOperationStatus>(spy.at(index).at(0));
}

/* 登录成功并等待会话建立（默认手机号资料，userId = phone） */
void loginAs(MockUserNetworkApi &network, UserService &service,
             const QSignalSpy *states = nullptr)
{
    LoginResult login;
    login.session.profile.userId = QStringLiteral("13800138000");
    login.session.profile.phone = QStringLiteral("13800138000");
    network.setLoginResult(login);
    service.loginByPhone(QStringLiteral("13800138000"));
    QTRY_VERIFY(service.currentSession().authenticated);
    if (states != nullptr) {
        QTest::qWait(0);   // 排空异步事件，保证后续断言基于稳定状态
    }
}

} // namespace

/* —— 操作状态：Idle -> Running -> Idle —— */
void UserModuleTests::publishesRunningAndIdleForLogin()
{
    MockUserNetworkApi network;
    UserService service(&network);
    QSignalSpy states(&service, &IUserService::operationStatusChanged);

    loginAs(network, service);

    // 普通登录只发布真实变化：Login Running -> Login Idle（评审意见 P2）
    QCOMPARE(states.count(), 2);
    const UserOperationStatus running = statusAt(states, 0);
    QCOMPARE(running.operation, UserOperation::Login);
    QCOMPARE(running.state, UserOperationState::Running);
    QVERIFY(!running.requestId.isEmpty());

    const UserOperationStatus idle = statusAt(states, 1);
    QCOMPARE(idle.operation, UserOperation::Login);
    QCOMPARE(idle.state, UserOperationState::Idle);
    QCOMPARE(idle.requestId, running.requestId);
    QCOMPARE(service.operationStatus(UserOperation::Login).state,
             UserOperationState::Idle);
}

void UserModuleTests::duplicateLoginKeepsOriginalRunningState()
{
    MockUserNetworkApi network;
    MockUserNetworkApi::Behavior delayed;
    delayed.delayMs = 50;
    network.setLoginBehavior(delayed);
    UserService service(&network);
    QSignalSpy states(&service, &IUserService::operationStatusChanged);
    QSignalSpy errors(&service, &IUserService::operationFailed);

    service.loginByPhone(QStringLiteral("13800138000"));
    const QString originalRequestId = service.operationStatus(UserOperation::Login).requestId;
    QVERIFY(!originalRequestId.isEmpty());

    service.loginByPhone(QStringLiteral("13800138000"));

    // 重复提交被拒但不覆盖原状态与原请求 ID
    QCOMPARE(errors.count(), 1);
    QCOMPARE(network.loginRequestCount(), 1);
    QCOMPARE(service.operationStatus(UserOperation::Login).state,
             UserOperationState::Running);
    QCOMPARE(service.operationStatus(UserOperation::Login).requestId, originalRequestId);
    QCOMPARE(states.count(), 1);
}

void UserModuleTests::failedLoginReturnsOperationToIdle()
{
    MockUserNetworkApi network;
    MockUserNetworkApi::Behavior failure;
    failure.outcome = MockUserNetworkApi::Outcome::Failure;
    network.setLoginBehavior(failure);
    UserService service(&network);
    QSignalSpy states(&service, &IUserService::operationStatusChanged);

    service.loginByPhone(QStringLiteral("13800138000"));
    QTRY_COMPARE(states.count(), 2);

    const UserOperationStatus idle = statusAt(states, 1);
    QCOMPARE(idle.state, UserOperationState::Idle);
    QVERIFY(!idle.requestId.isEmpty());
}

void UserModuleTests::unknownNicknameUpdatePublishesResultUnknown()
{
    MockUserNetworkApi network;
    UserService service(&network);
    QSignalSpy states(&service, &IUserService::operationStatusChanged);
    loginAs(network, service);

    MockUserNetworkApi::Behavior unknown;
    unknown.outcome = MockUserNetworkApi::Outcome::ResultUnknown;
    network.setNicknameBehavior(unknown);
    service.updateNickname(QStringLiteral("Alice"));

    QTRY_COMPARE(states.count(), 3);   // Login 2 事件 + UpdateNickname Running
    const UserOperationStatus running = statusAt(states, 2);
    QCOMPARE(running.operation, UserOperation::UpdateNickname);
    QCOMPARE(running.state, UserOperationState::Running);

    QTRY_COMPARE(states.count(), 4);
    const UserOperationStatus resultUnknown = statusAt(states, 3);
    QCOMPARE(resultUnknown.state, UserOperationState::ResultUnknown);
    QVERIFY(!resultUnknown.operationId.isEmpty());
    QCOMPARE(service.operationStatus(UserOperation::UpdateNickname).state,
             UserOperationState::ResultUnknown);
}

void UserModuleTests::failedNicknameUpdateReturnsOperationToIdle()
{
    MockUserNetworkApi network;
    UserService service(&network);
    QSignalSpy states(&service, &IUserService::operationStatusChanged);
    loginAs(network, service);

    MockUserNetworkApi::Behavior failure;
    failure.outcome = MockUserNetworkApi::Outcome::Failure;
    network.setNicknameBehavior(failure);
    service.updateNickname(QStringLiteral("Alice"));

    QTRY_COMPARE(states.count(), 4);
    QCOMPARE(statusAt(states, 3).state, UserOperationState::Idle);
    QCOMPARE(service.operationStatus(UserOperation::UpdateNickname).state,
             UserOperationState::Idle);
}

void UserModuleTests::successfulProfileRefreshClearsResultUnknownState()
{
    MockUserNetworkApi network;
    UserService service(&network);
    QSignalSpy states(&service, &IUserService::operationStatusChanged);
    loginAs(network, service);

    MockUserNetworkApi::Behavior unknown;
    unknown.outcome = MockUserNetworkApi::Outcome::ResultUnknown;
    network.setNicknameBehavior(unknown);
    service.updateNickname(QStringLiteral("Alice"));
    QTRY_COMPARE(service.operationStatus(UserOperation::UpdateNickname).state,
                 UserOperationState::ResultUnknown);

    UserProfileResult confirmed;
    confirmed.profile.userId = QStringLiteral("13800138000");
    confirmed.profile.phone = QStringLiteral("13800138000");
    confirmed.profile.nickname = QStringLiteral("Alice");
    confirmed.accountStatus = AccountStatus::Normal;
    network.setUserProfileResult(confirmed);
    service.refreshCurrentUser();
    QTRY_COMPARE(service.currentSession().profile.nickname, QStringLiteral("Alice"));

    // 典型序列：ResultUnknown -> RefreshProfile Running -> UpdateNickname Idle
    QTRY_COMPARE(service.operationStatus(UserOperation::UpdateNickname).state,
                 UserOperationState::Idle);
    QCOMPARE(service.operationStatus(UserOperation::RefreshProfile).state,
             UserOperationState::Idle);
}

void UserModuleTests::failedProfileRefreshKeepsResultUnknown()
{
    MockUserNetworkApi network;
    UserService service(&network);
    loginAs(network, service);

    MockUserNetworkApi::Behavior unknown;
    unknown.outcome = MockUserNetworkApi::Outcome::ResultUnknown;
    network.setNicknameBehavior(unknown);
    service.updateNickname(QStringLiteral("Alice"));
    QTRY_COMPARE(service.operationStatus(UserOperation::UpdateNickname).state,
                 UserOperationState::ResultUnknown);

    QSignalSpy errors(&service, &IUserService::operationFailed);
    MockUserNetworkApi::Behavior failure;
    failure.outcome = MockUserNetworkApi::Outcome::Failure;
    network.setQueryBehavior(failure);
    service.refreshCurrentUser();

    // 同时等待失败信号与终态，防止断言在失败回调执行前假通过（评审意见 P2）
    QTRY_COMPARE(errors.count(), 1);
    QCOMPARE(service.operationStatus(UserOperation::RefreshProfile).state,
             UserOperationState::Idle);
    QCOMPARE(service.operationStatus(UserOperation::UpdateNickname).state,
             UserOperationState::ResultUnknown);

    // 结果未知未被解除，更新仍被锁定
    service.updateNickname(QStringLiteral("Bob"));
    QCOMPARE(errors.count(), 2);
    QCOMPARE(qvariant_cast<ClientError>(errors.takeLast().at(0)).code,
             QStringLiteral("result-unknown-pending"));
}

void UserModuleTests::profileRefreshDoesNotFinishRunningNicknameUpdate()
{
    MockUserNetworkApi network;
    UserService service(&network);
    loginAs(network, service);

    UserProfileResult updateResult;
    updateResult.profile.userId = QStringLiteral("13800138000");
    network.setUserProfileResult(updateResult);

    MockUserNetworkApi::Behavior delayedUpdate;
    delayedUpdate.delayMs = 100;
    network.setNicknameBehavior(delayedUpdate);
    service.updateNickname(QStringLiteral("Alice"));
    QCOMPARE(service.operationStatus(UserOperation::UpdateNickname).state,
             UserOperationState::Running);

    UserProfileResult refreshed;
    refreshed.profile.userId = QStringLiteral("13800138000");
    refreshed.profile.phone = QStringLiteral("13800138000");
    refreshed.profile.nickname = QStringLiteral("ServerName");
    refreshed.accountStatus = AccountStatus::Normal;
    network.setUserProfileResult(refreshed);

    QSignalSpy refreshes(&service, &IUserService::currentUserRefreshed);
    QSignalSpy nicknameUpdates(&service, &IUserService::nicknameUpdated);
    service.refreshCurrentUser();
    QTRY_COMPARE(refreshes.count(), 1);

    // 资料刷新完成不代表并发的昵称修改已完成。
    QCOMPARE(nicknameUpdates.count(), 0);
    QCOMPARE(service.operationStatus(UserOperation::UpdateNickname).state,
             UserOperationState::Running);

    QTRY_COMPARE(nicknameUpdates.count(), 1);
    QCOMPARE(service.operationStatus(UserOperation::UpdateNickname).state,
             UserOperationState::Idle);
    QCOMPARE(service.currentSession().profile.nickname, QStringLiteral("Alice"));
}

void UserModuleTests::logoutClearsAllOperationStates()
{
    MockUserNetworkApi network;
    UserService service(&network);
    loginAs(network, service);

    MockUserNetworkApi::Behavior unknown;
    unknown.outcome = MockUserNetworkApi::Outcome::ResultUnknown;
    network.setNicknameBehavior(unknown);
    service.updateNickname(QStringLiteral("Alice"));
    QTRY_COMPARE(service.operationStatus(UserOperation::UpdateNickname).state,
                 UserOperationState::ResultUnknown);

    service.logout();
    QTRY_VERIFY(service.operationStatus(UserOperation::Logout).state
                == UserOperationState::Idle);

    QVERIFY(service.operationStatus(UserOperation::Login).state
            == UserOperationState::Idle);
    QVERIFY(service.operationStatus(UserOperation::RefreshProfile).state
            == UserOperationState::Idle);
    QVERIFY(service.operationStatus(UserOperation::UpdateNickname).state
            == UserOperationState::Idle);
    QVERIFY(service.operationStatus(UserOperation::Logout).state
            == UserOperationState::Idle);
    QVERIFY(!service.currentSession().authenticated);
}

void UserModuleTests::logoutDoesNotPublishIdleForNeverStartedOperations()
{
    MockUserNetworkApi network;
    UserService service(&network);
    loginAs(network, service);

    QSignalSpy states(&service, &IUserService::operationStatusChanged);
    service.logout();
    QTRY_COMPARE(service.operationStatus(UserOperation::Logout).state,
                 UserOperationState::Idle);

    // RefreshProfile 和 UpdateNickname 从未执行，clearSession 不应为它们
    // 广播虚假的 Idle 状态变化。
    for (const QList<QVariant> &arguments : states) {
        const UserOperationStatus status =
            qvariant_cast<UserOperationStatus>(arguments.at(0));
        QVERIFY(status.operation != UserOperation::RefreshProfile);
        QVERIFY(status.operation != UserOperation::UpdateNickname);
    }
}

void UserModuleTests::lateResponseDoesNotChangeOperationState()
{
    MockUserNetworkApi network;
    MockUserNetworkApi::Behavior delayedLogin;
    delayedLogin.delayMs = 50;
    network.setLoginBehavior(delayedLogin);
    UserService service(&network);
    QSignalSpy successes(&service, &IUserService::loginSucceeded);

    service.loginByPhone(QStringLiteral("13800138000"));
    service.logout();

    QTest::qWait(80);
    // 迟到的登录成功不得发布信号，也不得改变操作状态
    QCOMPARE(successes.count(), 0);
    QVERIFY(!service.currentSession().authenticated);
    QCOMPARE(service.operationStatus(UserOperation::Login).state,
             UserOperationState::Idle);
    QVERIFY(service.operationStatus(UserOperation::Login).requestId.isEmpty());
}

void UserModuleTests::profileRefreshKeepsSessionAuthenticated()
{
    MockUserNetworkApi network;
    UserService service(&network);
    loginAs(network, service);

    UserProfileResult refreshed;
    refreshed.profile.userId = QStringLiteral("13800138000");
    refreshed.profile.phone = QStringLiteral("13800138000");
    refreshed.profile.nickname = QStringLiteral("老王");
    refreshed.accountStatus = AccountStatus::Frozen;
    network.setUserProfileResult(refreshed);
    service.refreshCurrentUser();

    QTRY_COMPARE(service.currentSession().profile.nickname, QStringLiteral("老王"));
    QCOMPARE(service.currentSession().accountStatus, AccountStatus::Frozen);
    // 资料刷新不得清除会话认证状态
    QVERIFY(service.currentSession().authenticated);
}

void UserModuleTests::rejectsProfileForDifferentUser()
{
    MockUserNetworkApi network;
    UserService service(&network);
    QSignalSpy errors(&service, &IUserService::operationFailed);
    loginAs(network, service);

    // 先制造改昵称结果未知
    MockUserNetworkApi::Behavior unknown;
    unknown.outcome = MockUserNetworkApi::Outcome::ResultUnknown;
    network.setNicknameBehavior(unknown);
    service.updateNickname(QStringLiteral("Alice"));
    QTRY_COMPARE(service.operationStatus(UserOperation::UpdateNickname).state,
                 UserOperationState::ResultUnknown);

    // 应答用户与当前会话不符：不合并会话、不解除结果未知
    UserProfileResult mismatched;
    mismatched.profile.userId = QStringLiteral("U99999999999");
    mismatched.profile.phone = QStringLiteral("13900000000");
    mismatched.profile.nickname = QStringLiteral("他人资料");
    mismatched.accountStatus = AccountStatus::Normal;
    network.setUserProfileResult(mismatched);
    service.refreshCurrentUser();

    QTRY_COMPARE(errors.count(), 2);   // 结果未知 1 次 + 串号 1 次
    const ClientError mismatch =
        qvariant_cast<ClientError>(errors.takeLast().at(0));
    QCOMPARE(mismatch.code, QStringLiteral("profile-user-mismatch"));

    QCOMPARE(service.currentSession().profile.userId, QStringLiteral("13800138000"));
    QVERIFY(service.currentSession().profile.nickname.isEmpty());
    QCOMPARE(service.currentSession().accountStatus, AccountStatus::Unknown);
    QVERIFY(service.currentSession().authenticated);
    QCOMPARE(service.operationStatus(UserOperation::RefreshProfile).state,
             UserOperationState::Idle);
    QCOMPARE(service.operationStatus(UserOperation::UpdateNickname).state,
             UserOperationState::ResultUnknown);
}

void UserModuleTests::reLoginClearsPreviousOperationContexts()
{
    MockUserNetworkApi network;
    UserService service(&network);
    loginAs(network, service);

    UserProfileResult profile;
    profile.profile.userId = QStringLiteral("13800138000");
    profile.profile.phone = QStringLiteral("13800138000");
    profile.profile.nickname = QStringLiteral("Alice");
    network.setUserProfileResult(profile);

    service.refreshCurrentUser();
    QTRY_COMPARE(service.currentSession().profile.nickname, QStringLiteral("Alice"));
    service.updateNickname(QStringLiteral("Alice2"));
    QTRY_COMPARE(service.currentSession().profile.nickname, QStringLiteral("Alice2"));

    QVERIFY(!service.operationStatus(UserOperation::RefreshProfile).requestId.isEmpty());
    QVERIFY(!service.operationStatus(UserOperation::UpdateNickname).requestId.isEmpty());

    LoginResult secondLogin;
    secondLogin.session.profile.userId = QStringLiteral("13900139000");
    secondLogin.session.profile.phone = QStringLiteral("13900139000");
    network.setLoginResult(secondLogin);
    service.loginByPhone(QStringLiteral("13900139000"));
    QTRY_COMPARE(service.currentSession().profile.userId,
                 QStringLiteral("13900139000"));

    QCOMPARE(service.operationStatus(UserOperation::RefreshProfile).state,
             UserOperationState::Idle);
    QVERIFY(service.operationStatus(UserOperation::RefreshProfile).requestId.isEmpty());
    QCOMPARE(service.operationStatus(UserOperation::UpdateNickname).state,
             UserOperationState::Idle);
    QVERIFY(service.operationStatus(UserOperation::UpdateNickname).requestId.isEmpty());
}

/* —— 昵称校验 —— */
void UserModuleTests::rejectsEmptyNickname()
{
    MockUserNetworkApi network;
    UserService service(&network);
    QSignalSpy errors(&service, &IUserService::operationFailed);
    QSignalSpy states(&service, &IUserService::operationStatusChanged);
    loginAs(network, service);
    const int statesBefore = states.count();

    service.updateNickname(QStringLiteral("   "));

    QCOMPARE(errors.count(), 1);
    QCOMPARE(qvariant_cast<ClientError>(errors.takeFirst().at(0)).code,
             QStringLiteral("invalid-nickname-empty"));
    QCOMPARE(network.nicknameRequestCount(), 0);
    // 本地校验失败不进入 Running
    QCOMPARE(service.operationStatus(UserOperation::UpdateNickname).state,
             UserOperationState::Idle);
    QCOMPARE(states.count(), statesBefore);
}

void UserModuleTests::rejectsNicknameLongerThanTwentyCharacters()
{
    MockUserNetworkApi network;
    UserService service(&network);
    QSignalSpy errors(&service, &IUserService::operationFailed);
    loginAs(network, service);

    // QChar(0x738B) = '王'：QLatin1Char 只能表示单字节 Latin-1 字符
    service.updateNickname(QString(21, QChar(0x738B)));

    QCOMPARE(errors.count(), 1);
    QCOMPARE(qvariant_cast<ClientError>(errors.takeFirst().at(0)).code,
             QStringLiteral("invalid-nickname-too-long"));
    QCOMPARE(network.nicknameRequestCount(), 0);
}

void UserModuleTests::rejectsNicknameWithControlCharacters()
{
    MockUserNetworkApi network;
    UserService service(&network);
    QSignalSpy errors(&service, &IUserService::operationFailed);
    loginAs(network, service);

    service.updateNickname(QStringLiteral("老王\n二号"));

    QCOMPARE(errors.count(), 1);
    QCOMPARE(qvariant_cast<ClientError>(errors.takeFirst().at(0)).code,
             QStringLiteral("invalid-nickname-control-character"));
    QCOMPARE(network.nicknameRequestCount(), 0);
}

void UserModuleTests::trimsNicknameBeforeSending()
{
    MockUserNetworkApi network;
    UserService service(&network);
    loginAs(network, service);

    network.setNicknameBehavior(MockUserNetworkApi::Behavior{});
    service.updateNickname(QStringLiteral("  老王  "));
    QTRY_COMPARE(network.nicknameRequestCount(), 1);
    QCOMPARE(network.lastNickname(), QStringLiteral("老王"));
    QTRY_COMPARE(service.currentSession().profile.nickname, QStringLiteral("老王"));
}

QTEST_MAIN(UserModuleTests)

#include "user-module-tests.moc"
