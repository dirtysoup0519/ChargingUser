#include <QtTest>

#include "app/application.h"
#include "app/iuseruibinder.h"
#include "app/mapuibinder.h"
#include "demo/userdemocontroller.h"
#include "modules/charger/mockchargerservice.h"
#include "modules/map/mockmapservice.h"
#include "modules/user/mockusernetworkapi.h"
#include "presentation/pages/auth/loginwindow.h"
#include "presentation/pages/home/stationdetailwindow.h"
#include "presentation/pages/shell/mainwindow.h"
#include "presentation/pages/profile/profileeditwindow.h"
#include "presentation/widgets/map/interactivemapwidget.h"

#include <QApplication>
#include <QLabel>
#include <QFile>
#include <QFrame>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QStackedWidget>
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
    void primaryNavigationMatchesRequestedPages();
    void stationAndRechargeNavigationMatchesPages();
    void homeRendersMapLocationAndStationStates();
    void draggingFallbackMapProducesSearchableBounds();
    void detailRefreshKeepsLastSuccessfulChargers();
    void oldUserReloginKeepsConfiguredNickname();
    void newUserCompletesNicknameAndReachesHome();
    void newUserReloginUsesExistingUserAndSavedNickname();
    void editedNicknameSurvivesRelogin();
    void networkFailureKeepsInputAndSecondSubmitSucceeds();
    void frozenUserShowsRestrictionButCanRecharge();
    void logoutReturnsToClearedLoginPage();

private:
    void submitPhone(const QString &phone);
    void waitForMainWindow();
    QString currentMainPageName() const;

    MockUserNetworkApi *m_network = nullptr;
    MockChargerService *m_chargerService = nullptr;
    MockMapService *m_mapService = nullptr;
    MapUiBinder *m_mapBinder = nullptr;
    UserApplicationAssembly *m_assembly = nullptr;
    LoginWindow *m_login = nullptr;
    ProfileEditWindow *m_profileEdit = nullptr;
    MainWindow *m_mainWindow = nullptr;
    UserDemoController *m_controller = nullptr;
};

void UserDemoTests::init()
{
    qRegisterMetaType<GeoBounds>();
    m_network = new MockUserNetworkApi(this);
    m_assembly = new UserApplicationAssembly(m_network, this);
    m_chargerService = new MockChargerService(this);
    m_mapService = new MockMapService(this);
    StationDetail station;
    station.stationId = QStringLiteral("station-test");
    station.summary.stationId = station.stationId;
    station.summary.name = QStringLiteral("Test station");
    station.summary.address = QStringLiteral("Test address");
    station.summary.point = GeoPoint{22.51, 114.01};
    station.summary.availableCount = 1;
    station.summary.totalCount = 1;
    ChargerSummary charger;
    charger.chargerId = QStringLiteral("charger-test");
    charger.online = true;
    charger.businessStatus = ChargerBusinessStatus::Idle;
    charger.canStartCharging = true;
    station.chargers.append(charger);
    m_chargerService->setStationCatalog({station});
    LocationResult location;
    location.point = {22.50, 114.00};
    location.capturedAtUtc = QDateTime::currentDateTimeUtc();
    m_mapService->setLocationResult(location);
    RouteResult route;
    route.routeId = QStringLiteral("route-test");
    route.polyline = {location.point, *station.summary.point};
    route.distanceMeters = 1200;
    route.durationSeconds = 600;
    m_mapService->setRouteResult(station.stationId, TravelMode::Driving, route);
    m_mapBinder = new MapUiBinder(m_chargerService, m_mapService, this);
    m_login = new LoginWindow;
    m_profileEdit = new ProfileEditWindow;
    m_mainWindow = new MainWindow;
    m_controller = new UserDemoController(m_network, m_assembly->userUiBinder(),
                                          m_mapBinder,
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
    delete m_mapBinder;
    delete m_mapService;
    delete m_chargerService;
    delete m_network;

    m_controller = nullptr;
    m_mainWindow = nullptr;
    m_profileEdit = nullptr;
    m_login = nullptr;
    m_assembly = nullptr;
    m_mapBinder = nullptr;
    m_mapService = nullptr;
    m_chargerService = nullptr;
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
    QCOMPARE(currentMainPageName(), QStringLiteral("homePage"));

    QStackedWidget *pageStack = m_mainWindow->findChild<QStackedWidget *>(
        QStringLiteral("pageStack"));
    QVERIFY(pageStack);
    const QStringList secondaryPageNames = {
        QStringLiteral("StationDetailWindow"),
        QStringLiteral("NavigationWindow"),
        QStringLiteral("WalletRechargeWindow")
    };
    for (const QString &pageName : secondaryPageNames) {
        QWidget *page = m_mainWindow->findChild<QWidget *>(pageName);
        QVERIFY2(page, qPrintable(pageName));
        QCOMPARE(page->parentWidget(), pageStack);
        QVERIFY2(!page->isVisible(), qPrintable(pageName));
    }
}

QString UserDemoTests::currentMainPageName() const
{
    const QStackedWidget *pageStack = m_mainWindow->findChild<QStackedWidget *>(
        QStringLiteral("pageStack"));
    if (!pageStack || !pageStack->currentWidget())
        return {};
    return pageStack->currentWidget()->objectName();
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

void UserDemoTests::primaryNavigationMatchesRequestedPages()
{
    submitPhone(userForScenario(QStringLiteral("normal"))
                    .value(QStringLiteral("phone")).toString());
    waitForMainWindow();

    QToolButton *chargeNav = m_mainWindow->findChild<QToolButton *>(
        QStringLiteral("chargeNav"));
    QToolButton *profileNav = m_mainWindow->findChild<QToolButton *>(
        QStringLiteral("profileNav"));
    QToolButton *homeNav = m_mainWindow->findChild<QToolButton *>(
        QStringLiteral("homeNav"));
    QVERIFY(chargeNav);
    QVERIFY(profileNav);
    QVERIFY(homeNav);

    QTest::mouseClick(chargeNav, Qt::LeftButton);
    QCOMPARE(currentMainPageName(), QStringLiteral("chargingPage"));
    QTest::mouseClick(profileNav, Qt::LeftButton);
    QCOMPARE(currentMainPageName(), QStringLiteral("profilePage"));
    QTest::mouseClick(homeNav, Qt::LeftButton);
    QCOMPARE(currentMainPageName(), QStringLiteral("homePage"));
}

void UserDemoTests::stationAndRechargeNavigationMatchesPages()
{
    submitPhone(userForScenario(QStringLiteral("normal"))
                    .value(QStringLiteral("phone")).toString());
    waitForMainWindow();

    QTRY_VERIFY_WITH_TIMEOUT(
        m_mainWindow->findChild<QPushButton *>(QStringLiteral("stationDetailsButton")),
        1000);
    QPushButton *station = m_mainWindow->findChild<QPushButton *>(
        QStringLiteral("stationDetailsButton"));
    QTest::mouseClick(station, Qt::LeftButton);
    QCOMPARE(currentMainPageName(), QStringLiteral("StationDetailWindow"));

    QPushButton *navigate = m_mainWindow->findChild<QPushButton *>(
        QStringLiteral("navigationButton"));
    QVERIFY(navigate);
    QTRY_VERIFY_WITH_TIMEOUT(navigate->isEnabled(), 1000);
    QTest::mouseClick(navigate, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(currentMainPageName(),
                              QStringLiteral("NavigationWindow"), 1000);

    QWidget *navigationPage = m_mainWindow->findChild<QWidget *>(
        QStringLiteral("NavigationWindow"));
    QVERIFY(navigationPage);
    QPushButton *navigationBack = navigationPage->findChild<QPushButton *>(
        QStringLiteral("backButton"));
    QVERIFY(navigationBack);
    QTest::mouseClick(navigationBack, Qt::LeftButton);
    QCOMPARE(currentMainPageName(), QStringLiteral("StationDetailWindow"));

    QWidget *stationPage = m_mainWindow->findChild<QWidget *>(
        QStringLiteral("StationDetailWindow"));
    QVERIFY(stationPage);
    QPushButton *stationBack = stationPage->findChild<QPushButton *>(
        QStringLiteral("backButton"));
    QVERIFY(stationBack);
    QTest::mouseClick(stationBack, Qt::LeftButton);
    QCOMPARE(currentMainPageName(), QStringLiteral("homePage"));

    QToolButton *profileNav = m_mainWindow->findChild<QToolButton *>(
        QStringLiteral("profileNav"));
    QVERIFY(profileNav);
    QTest::mouseClick(profileNav, Qt::LeftButton);
    QCOMPARE(currentMainPageName(), QStringLiteral("profilePage"));

    QPushButton *recharge = m_mainWindow->findChild<QPushButton *>(
        QStringLiteral("btnRecharge"));
    QVERIFY(recharge);
    QTest::mouseClick(recharge, Qt::LeftButton);
    QCOMPARE(currentMainPageName(), QStringLiteral("WalletRechargeWindow"));

    QWidget *rechargePage = m_mainWindow->findChild<QWidget *>(
        QStringLiteral("WalletRechargeWindow"));
    QVERIFY(rechargePage);
    QPushButton *rechargeBack = rechargePage->findChild<QPushButton *>(
        QStringLiteral("backButton"));
    QVERIFY(rechargeBack);
    QTest::mouseClick(rechargeBack, Qt::LeftButton);
    QCOMPARE(currentMainPageName(), QStringLiteral("profilePage"));
}

void UserDemoTests::homeRendersMapLocationAndStationStates()
{
    HomeMapViewState state;
    state.mapStatus = MapLoadStatus::Error;
    state.mapMessage = QStringLiteral("地图加载失败");
    state.canRetryMap = true;
    state.locationStatus = MapLoadStatus::Error;
    state.locationMessage = QStringLiteral("定位不可用");
    state.canRetryLocation = true;
    state.stationsStatus = MapLoadStatus::Error;
    state.stationsMessage = QStringLiteral("站点加载失败");
    state.canRetryStations = true;
    m_mainWindow->renderHome(state);

    QLabel *message = m_mainWindow->findChild<QLabel *>(QStringLiteral("searchErrorLabel"));
    QPushButton *mapRetry = m_mainWindow->findChild<QPushButton *>(QStringLiteral("mapRetryButton"));
    QPushButton *locationRetry = m_mainWindow->findChild<QPushButton *>(QStringLiteral("locationRetryButton"));
    QPushButton *stationRetry = m_mainWindow->findChild<QPushButton *>(QStringLiteral("searchRetryButton"));
    QVERIFY(message);
    QVERIFY(mapRetry);
    QVERIFY(locationRetry);
    QVERIFY(stationRetry);
    QVERIFY(message->text().contains(QStringLiteral("地图加载失败")));
    QVERIFY(message->text().contains(QStringLiteral("定位不可用")));
    QVERIFY(message->text().contains(QStringLiteral("站点加载失败")));
    QVERIFY(!mapRetry->isHidden());
    QVERIFY(!locationRetry->isHidden());
    QVERIFY(!stationRetry->isHidden());
}

void UserDemoTests::draggingFallbackMapProducesSearchableBounds()
{
    submitPhone(userForScenario(QStringLiteral("normal"))
                    .value(QStringLiteral("phone")).toString());
    waitForMainWindow();
    QTRY_VERIFY(m_mapBinder->currentHomeState().camera.bounds.has_value());
    const GeoBounds original = *m_mapBinder->currentHomeState().camera.bounds;

    InteractiveMapWidget *map = m_mainWindow->findChild<InteractiveMapWidget *>(
        QStringLiteral("mapView"));
    QVERIFY(map);
    QSignalSpy searchSpy(m_mainWindow, &MainWindow::searchAreaRequested);
    const QPoint start(100, 100);
    const QPoint end(135, 115);
    QTest::mousePress(map, Qt::LeftButton, Qt::NoModifier, start);
    QMouseEvent move(QEvent::MouseMove, QPointF(end), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(map, &move);
    QTest::mouseRelease(map, Qt::LeftButton, Qt::NoModifier, end);

    QPushButton *searchArea = map->findChild<QPushButton *>(
        QStringLiteral("btnSearchArea"));
    QVERIFY(searchArea);
    QVERIFY(!searchArea->isHidden());
    QTest::mouseClick(searchArea, Qt::LeftButton);
    QCOMPARE(searchSpy.count(), 1);
    const GeoBounds shifted = searchSpy.first().at(0).value<GeoBounds>();
    QVERIFY(shifted.isValid());
    QVERIFY(shifted.southWest.latitude != original.southWest.latitude
            || shifted.southWest.longitude != original.southWest.longitude);
}

void UserDemoTests::detailRefreshKeepsLastSuccessfulChargers()
{
    submitPhone(userForScenario(QStringLiteral("normal"))
                    .value(QStringLiteral("phone")).toString());
    waitForMainWindow();
    QTRY_VERIFY_WITH_TIMEOUT(
        m_mainWindow->findChild<QPushButton *>(QStringLiteral("stationDetailsButton")), 1000);
    QTest::mouseClick(m_mainWindow->findChild<QPushButton *>(
                          QStringLiteral("stationDetailsButton")),
                      Qt::LeftButton);

    StationDetailWindow *detail = m_mainWindow->findChild<StationDetailWindow *>();
    QVERIFY(detail);
    QTRY_COMPARE_WITH_TIMEOUT(detail->findChildren<QFrame *>(
                                  QStringLiteral("chargerRow")).size(), 1, 1000);

    MockChargerService::Behavior behavior;
    behavior.responseDelayMs = 100;
    behavior.timeoutMs = 500;
    m_chargerService->setStationDetailBehavior(behavior);
    QPushButton *retry = detail->findChild<QPushButton *>(
        QStringLiteral("detailRetryButton"));
    QVERIFY(retry);
    emit detail->stationRefreshRequested();

    QCOMPARE(m_mapBinder->currentStationDetailState().status,
             MapLoadStatus::Loading);
    QCOMPARE(detail->findChildren<QFrame *>(QStringLiteral("chargerRow")).size(), 1);
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

void UserDemoTests::newUserReloginUsesExistingUserAndSavedNickname()
{
    const QString phone = testValue(QStringLiteral("newUserPhone"));
    const QString savedNickname = testValue(QStringLiteral("newUserSavedNickname"));
    submitPhone(phone);
    QTRY_VERIFY_WITH_TIMEOUT(m_profileEdit->isVisible(), 3000);

    QLineEdit *nickname = m_profileEdit->findChild<QLineEdit *>(
        QStringLiteral("nicknameEdit"));
    QPushButton *save = m_profileEdit->findChild<QPushButton *>(
        QStringLiteral("saveButton"));
    QVERIFY(nickname);
    QVERIFY(save);
    nickname->setText(savedNickname);
    QTest::mouseClick(save, Qt::LeftButton);
    waitForMainWindow();

    QToolButton *profileNav = m_mainWindow->findChild<QToolButton *>(
        QStringLiteral("profileNav"));
    QPushButton *logout = m_mainWindow->findChild<QPushButton *>(
        QStringLiteral("btnLogout"));
    QVERIFY(profileNav);
    QVERIFY(logout);
    QTest::mouseClick(profileNav, Qt::LeftButton);
    QTest::mouseClick(logout, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(m_login->isVisible(), 2000);

    submitPhone(phone);
    waitForMainWindow();
    QVERIFY(!m_profileEdit->isVisible());
    QTest::mouseClick(profileNav, Qt::LeftButton);
    QLabel *summary = m_mainWindow->findChild<QLabel *>(
        QStringLiteral("profileSummaryLabel"));
    QVERIFY(summary);
    QCOMPARE(summary->text(), savedNickname);
}

void UserDemoTests::editedNicknameSurvivesRelogin()
{
    const QString phone = userForScenario(QStringLiteral("normal"))
                              .value(QStringLiteral("phone")).toString();
    const QString editedNickname = testValue(QStringLiteral("editedNickname"));
    submitPhone(phone);
    waitForMainWindow();

    QToolButton *profileNav = m_mainWindow->findChild<QToolButton *>(
        QStringLiteral("profileNav"));
    QToolButton *editProfile = m_mainWindow->findChild<QToolButton *>(
        QStringLiteral("btnEditProfile"));
    QVERIFY(profileNav);
    QVERIFY(editProfile);
    QTest::mouseClick(profileNav, Qt::LeftButton);
    QTest::mouseClick(editProfile, Qt::LeftButton);
    QTRY_VERIFY(m_profileEdit->isVisible());

    QLineEdit *nickname = m_profileEdit->findChild<QLineEdit *>(
        QStringLiteral("nicknameEdit"));
    QPushButton *save = m_profileEdit->findChild<QPushButton *>(
        QStringLiteral("saveButton"));
    QPushButton *back = m_profileEdit->findChild<QPushButton *>(
        QStringLiteral("backButton"));
    QVERIFY(nickname);
    QVERIFY(save);
    QVERIFY(back);
    nickname->setText(editedNickname);
    QTest::mouseClick(save, Qt::LeftButton);
    QTRY_COMPARE(m_assembly->userUiBinder()->currentProfileEditViewState().submitState,
                 SubmitState::Success);
    QTest::mouseClick(back, Qt::LeftButton);
    QTRY_VERIFY(m_mainWindow->isVisible());

    QPushButton *logout = m_mainWindow->findChild<QPushButton *>(
        QStringLiteral("btnLogout"));
    QVERIFY(logout);
    QTest::mouseClick(logout, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(m_login->isVisible(), 2000);
    submitPhone(phone);
    waitForMainWindow();

    QTest::mouseClick(profileNav, Qt::LeftButton);
    QLabel *summary = m_mainWindow->findChild<QLabel *>(
        QStringLiteral("profileSummaryLabel"));
    QVERIFY(summary);
    QCOMPARE(summary->text(), editedNickname);
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
