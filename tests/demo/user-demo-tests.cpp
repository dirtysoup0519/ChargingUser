#include <QtTest>

#include "app/application.h"
#include "demo/userdemocontroller.h"
#include "modules/user/mockusernetworkapi.h"
#include "presentation/pages/auth/loginwindow.h"
#include "presentation/pages/shell/mainwindow.h"
#include "presentation/pages/profile/profileeditwindow.h"

#include <QLabel>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>

namespace
{

QJsonObject demoData()
{
    QFile file(QStringLiteral(":/demo/user-demo-data.tmp"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

QJsonObject userForScenario(const QString &scenario)
{
    for (const QJsonValue &value : demoData().value(QStringLiteral("users")).toArray()) {
        const QJsonObject user = value.toObject();
        if (user.value(QStringLiteral("scenario")).toString() == scenario)
            return user;
    }
    return {};
}

QString testValue(const QString &name)
{
    return demoData().value(QStringLiteral("testData")).toObject().value(name).toString();
}

} // namespace

class UserDemoTests final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void invalidPhoneKeepsInputAndShowsError();
    void oldUserReachesHomeAndProfileSummary();
    void oldUserReloginKeepsConfiguredNickname();
    void newUserCompletesNicknameAndReachesHome();
    void networkFailureKeepsInputAndSecondSubmitSucceeds();
    void frozenUserShowsRestrictionButCanRecharge();
    void logoutReturnsToClearedLoginPage();

private:
    void submitPhone(const QString &phone);
    void waitForMainWindow();

    MockUserNetworkApi *m_network = nullptr;
    UserApplicationAssembly *m_assembly = nullptr;
    LoginWindow *m_login = nullptr;
    ProfileEditWindow *m_profileEdit = nullptr;
    MainWindow *m_mainWindow = nullptr;
    UserDemoController *m_controller = nullptr;
};

void UserDemoTests::init()
{
    m_network = new MockUserNetworkApi(this);
    m_assembly = new UserApplicationAssembly(m_network, this);
    m_login = new LoginWindow;
    m_profileEdit = new ProfileEditWindow;
    m_mainWindow = new MainWindow;
    m_controller = new UserDemoController(m_network, m_assembly->userUiBinder(),
                                          m_login, m_profileEdit, m_mainWindow,
                                          this);
    m_controller->showInitialPage();
    QTRY_VERIFY(m_login->isVisible());
}

void UserDemoTests::cleanup()
{
    delete m_controller;
    delete m_mainWindow;
    delete m_profileEdit;
    delete m_login;
    delete m_assembly;
    delete m_network;

    m_controller = nullptr;
    m_mainWindow = nullptr;
    m_profileEdit = nullptr;
    m_login = nullptr;
    m_assembly = nullptr;
    m_network = nullptr;
}

void UserDemoTests::submitPhone(const QString &phone)
{
    QLineEdit *phoneEdit = m_login->findChild<QLineEdit *>(
        QStringLiteral("editPhoneNumber"));
    QPushButton *loginButton = m_login->findChild<QPushButton *>(
        QStringLiteral("btnLogin"));
    QVERIFY(phoneEdit);
    QVERIFY(loginButton);
    phoneEdit->setText(phone);
    QTest::mouseClick(loginButton, Qt::LeftButton);
}

void UserDemoTests::waitForMainWindow()
{
    QTRY_VERIFY_WITH_TIMEOUT(m_mainWindow->isVisible(), 3000);
    QVERIFY(!m_login->isVisible());
    QVERIFY(!m_profileEdit->isVisible());
}

void UserDemoTests::invalidPhoneKeepsInputAndShowsError()
{
    const QString invalidPhone = testValue(QStringLiteral("invalidPhone"));
    submitPhone(invalidPhone);

    QLineEdit *phoneEdit = m_login->findChild<QLineEdit *>(
        QStringLiteral("editPhoneNumber"));
    QLabel *error = m_login->findChild<QLabel *>(QStringLiteral("errorLabel"));
    QCOMPARE(phoneEdit->text(), invalidPhone);
    QVERIFY(error->isVisible());
    QVERIFY(!error->text().isEmpty());
}

void UserDemoTests::oldUserReachesHomeAndProfileSummary()
{
    const QJsonObject user = userForScenario(QStringLiteral("normal"));
    submitPhone(user.value(QStringLiteral("phone")).toString());
    QPushButton *loginButton = m_login->findChild<QPushButton *>(
        QStringLiteral("btnLogin"));
    QVERIFY(!loginButton->isEnabled());
    waitForMainWindow();

    QToolButton *profileNav = m_mainWindow->findChild<QToolButton *>(
        QStringLiteral("profileNav"));
    QVERIFY(profileNav);
    QTest::mouseClick(profileNav, Qt::LeftButton);
    QLabel *summary = m_mainWindow->findChild<QLabel *>(
        QStringLiteral("profileSummaryLabel"));
    QLabel *phone = m_mainWindow->findChild<QLabel *>(
        QStringLiteral("profilePhoneLabel"));
    QCOMPARE(summary->text(), user.value(QStringLiteral("nickname")).toString());
    const QString userPhone = user.value(QStringLiteral("phone")).toString();
    QCOMPARE(phone->text(), userPhone.left(3) + QStringLiteral("****") + userPhone.right(4));
}

void UserDemoTests::oldUserReloginKeepsConfiguredNickname()
{
    const QJsonObject user = userForScenario(QStringLiteral("normal"));
    const QString phone = user.value(QStringLiteral("phone")).toString();
    const QString nickname = user.value(QStringLiteral("nickname")).toString();

    submitPhone(phone);
    waitForMainWindow();
    QToolButton *profileNav = m_mainWindow->findChild<QToolButton *>(
        QStringLiteral("profileNav"));
    QPushButton *logout = m_mainWindow->findChild<QPushButton *>(
        QStringLiteral("btnLogout"));
    QVERIFY(profileNav);
    QTest::mouseClick(profileNav, Qt::LeftButton);
    QVERIFY(logout);
    QTest::mouseClick(logout, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(m_login->isVisible(), 2000);

    submitPhone(phone);
    waitForMainWindow();
    QTest::mouseClick(profileNav, Qt::LeftButton);
    QLabel *summary = m_mainWindow->findChild<QLabel *>(
        QStringLiteral("profileSummaryLabel"));
    QVERIFY(summary);
    QCOMPARE(summary->text(), nickname);
}

void UserDemoTests::newUserCompletesNicknameAndReachesHome()
{
    const QString newUserPhone = testValue(QStringLiteral("newUserPhone"));
    submitPhone(newUserPhone);
    QTRY_VERIFY_WITH_TIMEOUT(m_profileEdit->isVisible(), 3000);

    QLineEdit *phone = m_profileEdit->findChild<QLineEdit *>(
        QStringLiteral("phoneEdit"));
    QLineEdit *nickname = m_profileEdit->findChild<QLineEdit *>(
        QStringLiteral("nicknameEdit"));
    QPushButton *save = m_profileEdit->findChild<QPushButton *>(
        QStringLiteral("saveButton"));
    QVERIFY(phone);
    QVERIFY(nickname);
    QVERIFY(save);
    QCOMPARE(phone->text(), newUserPhone);
    QCOMPARE(nickname->text(), testValue(QStringLiteral("newUserGeneratedNickname")));

    nickname->setText(testValue(QStringLiteral("newUserSavedNickname")));
    QTest::mouseClick(save, Qt::LeftButton);
    QVERIFY(!save->isEnabled());
    waitForMainWindow();
}

void UserDemoTests::networkFailureKeepsInputAndSecondSubmitSucceeds()
{
    const QString retryPhone = userForScenario(QStringLiteral("retry"))
                                   .value(QStringLiteral("phone")).toString();
    submitPhone(retryPhone);

    QLabel *error = m_login->findChild<QLabel *>(QStringLiteral("errorLabel"));
    QPushButton *loginButton = m_login->findChild<QPushButton *>(
        QStringLiteral("btnLogin"));
    QLineEdit *phoneEdit = m_login->findChild<QLineEdit *>(
        QStringLiteral("editPhoneNumber"));
    QTRY_VERIFY_WITH_TIMEOUT(error->isVisible(), 2000);
    QCOMPARE(phoneEdit->text(), retryPhone);
    QVERIFY(loginButton->isEnabled());

    QTest::mouseClick(loginButton, Qt::LeftButton);
    waitForMainWindow();
}

void UserDemoTests::frozenUserShowsRestrictionButCanRecharge()
{
    submitPhone(userForScenario(QStringLiteral("frozen"))
                    .value(QStringLiteral("phone")).toString());
    waitForMainWindow();

    QToolButton *profileNav = m_mainWindow->findChild<QToolButton *>(
        QStringLiteral("profileNav"));
    QVERIFY(profileNav);
    QTest::mouseClick(profileNav, Qt::LeftButton);
    QLabel *status = m_mainWindow->findChild<QLabel *>(
        QStringLiteral("accountStatusLabel"));
    QPushButton *recharge = m_mainWindow->findChild<QPushButton *>(
        QStringLiteral("btnRecharge"));
    QVERIFY(status);
    QVERIFY(recharge);
    QVERIFY(status->isVisible());
    QVERIFY(!status->text().isEmpty());
    QVERIFY(recharge->isEnabled());
}

void UserDemoTests::logoutReturnsToClearedLoginPage()
{
    submitPhone(userForScenario(QStringLiteral("normal"))
                    .value(QStringLiteral("phone")).toString());
    waitForMainWindow();

    QToolButton *profileNav = m_mainWindow->findChild<QToolButton *>(
        QStringLiteral("profileNav"));
    QVERIFY(profileNav);
    QTest::mouseClick(profileNav, Qt::LeftButton);
    QPushButton *logout = m_mainWindow->findChild<QPushButton *>(
        QStringLiteral("btnLogout"));
    QVERIFY(logout);
    QTest::mouseClick(logout, Qt::LeftButton);

    QTRY_VERIFY_WITH_TIMEOUT(m_login->isVisible(), 2000);
    QLineEdit *phoneEdit = m_login->findChild<QLineEdit *>(
        QStringLiteral("editPhoneNumber"));
    QVERIFY(phoneEdit->text().isEmpty());
}

QTEST_MAIN(UserDemoTests)

#include "user-demo-tests.moc"
