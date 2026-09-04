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
    void propagatesRetryableNetworkFailure();
    void blocksRetryAfterUnknownProfileUpdate();
    void ignoresLateLoginAfterLogout();
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

QTEST_MAIN(UserModuleTests)

#include "user-module-tests.moc"
