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
};

void UserModuleTests::initTestCase()
{
    qRegisterMetaType<ClientError>();
    qRegisterMetaType<LoginResult>();
    qRegisterMetaType<UserSession>();
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

QTEST_MAIN(UserModuleTests)

#include "user-module-tests.moc"
