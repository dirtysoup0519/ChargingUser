#include <QtTest>

#include "app/application.h"
#include "demo/userdemocontroller.h"
#include "modules/user/mockusernetworkapi.h"
#include "presentation/pages/auth/loginwindow.h"
#include "presentation/pages/shell/mainwindow.h"
#include "presentation/pages/profile/profileeditwindow.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>

class UserDemoTests final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void invalidPhoneKeepsInputAndShowsError();
    void oldUserReachesHomeAndProfileSummary();
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
    submitPhone(QStringLiteral("123"));

    QLineEdit *phoneEdit = m_login->findChild<QLineEdit *>(
        QStringLiteral("editPhoneNumber"));
    QLabel *error = m_login->findChild<QLabel *>(QStringLiteral("errorLabel"));
    QCOMPARE(phoneEdit->text(), QStringLiteral("123"));
    QVERIFY(error->isVisible());
    QVERIFY(!error->text().isEmpty());
}

void UserDemoTests::oldUserReachesHomeAndProfileSummary()
{
    submitPhone(QStringLiteral("13800000000"));
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
    QCOMPARE(summary->text(), QStringLiteral("演示用户"));
    QCOMPARE(phone->text(), QStringLiteral("138****0000"));
}

void UserDemoTests::newUserCompletesNicknameAndReachesHome()
{
    submitPhone(QStringLiteral("13500005678"));
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
    QCOMPARE(phone->text(), QStringLiteral("13500005678"));
    QCOMPARE(nickname->text(), QStringLiteral("用户5678"));

    nickname->setText(QStringLiteral("新用户昵称"));
    QTest::mouseClick(save, Qt::LeftButton);
    QVERIFY(!save->isEnabled());
    waitForMainWindow();
}

void UserDemoTests::networkFailureKeepsInputAndSecondSubmitSucceeds()
{
    submitPhone(QStringLiteral("13700000000"));

    QLabel *error = m_login->findChild<QLabel *>(QStringLiteral("errorLabel"));
    QPushButton *loginButton = m_login->findChild<QPushButton *>(
        QStringLiteral("btnLogin"));
    QLineEdit *phoneEdit = m_login->findChild<QLineEdit *>(
        QStringLiteral("editPhoneNumber"));
    QTRY_VERIFY_WITH_TIMEOUT(error->isVisible(), 2000);
    QCOMPARE(phoneEdit->text(), QStringLiteral("13700000000"));
    QVERIFY(loginButton->isEnabled());

    QTest::mouseClick(loginButton, Qt::LeftButton);
    waitForMainWindow();
}

void UserDemoTests::frozenUserShowsRestrictionButCanRecharge()
{
    submitPhone(QStringLiteral("13900000000"));
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
    submitPhone(QStringLiteral("13800000000"));
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
