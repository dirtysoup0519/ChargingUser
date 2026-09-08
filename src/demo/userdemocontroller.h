#pragma once

#include "app/imapuibinder.h"
#include "flow/userflowtypes.h"
#include "presentation/contracts/chargingviewstates.h"
#include "presentation/contracts/reservationviewstates.h"
#include "presentation/contracts/scanviewstate.h"

#include <QObject>
#include <QHash>
#include <QSet>

class IUserUiBinder;
class IMapUiBinder;
class LoginWindow;
class MainWindow;
class MockUserNetworkApi;
class ProfileEditWindow;
class NavigationWindow;
class StationDetailWindow;
class WalletRechargeWindow;
class ChargeConfirmationWindow;
class ReservationConfirmationWindow;
class QrCodeScannerWindow;
class QWidget;
struct DemoUserData
{
    QString nickname;
    AccountStatus status = AccountStatus::Normal;
    bool failFirstLogin = false;
    QString firstLoginFailureCode;
    QString firstLoginFailureMessage;
    bool firstLoginFailureRetryable = false;
};

/* Demo 专用页面接线器。它只存在于 CONFIG+=user_demo 和 UI 集成测试中，
 * 正式入口不会使用模拟账号或模拟网络结果。
 */
class UserDemoController final : public QObject
{
public:
    UserDemoController(MockUserNetworkApi *network,
                       IUserUiBinder *binder,
                       IMapUiBinder *mapBinder,
                       LoginWindow *login,
                       ProfileEditWindow *profileEdit,
                       MainWindow *mainWindow,
                       QObject *parent = nullptr);

    void showInitialPage();

private:
    void configureLogin(const QString &phone);
    void configureNicknameSave(const QString &nickname);
    void rememberConfirmedLogin(const LoginResult &result);
    void rememberConfirmedNickname(const UserProfileResult &result);
    void handleNavigation(NavigationTarget target);
    void handleMapPage(MapPageTarget target, const QString &stationId);
    void renderStationDetailWithReservation(StationDetailViewState state);
    void renderHomeWithReservation(HomeMapViewState state);
    void showChargeConfirmation(const QString &stationId, const QString &chargerId);
    void showOnly(QWidget *target);

    MockUserNetworkApi *m_network;
    IUserUiBinder *m_binder;
    IMapUiBinder *m_mapBinder;
    LoginWindow *m_login;
    ProfileEditWindow *m_profileEdit;
    MainWindow *m_mainWindow;
    StationDetailWindow *m_stationDetail;
    NavigationWindow *m_navigation;
    WalletRechargeWindow *m_walletRecharge;
    ChargeConfirmationWindow *m_chargeConfirmation;
    ReservationConfirmationWindow *m_reservationConfirmation;
    QrCodeScannerWindow *m_qrScanner;
    ChargeConfirmationViewState m_chargeConfirmationState;
    ReservationConfirmationViewState m_reservationState;
    ScanViewState m_scanState;
    StationDetailViewState m_reservedDetailState;
    int m_reservationResponseDelayMs = 450;
    int m_reservationCancellationCooldownSeconds = 3;
    int m_cancellationResponseDelayMs = 450;
    QString m_cancellationOutcome;
    QString m_cancellationRetryOutcome;
    QString m_cancellationFailureMessage;
    QString m_cancellationUnknownMessage;
    QString m_reservationOutcome;
    bool m_walletOpenedFromConfirmation = false;
    bool m_profileEditOpenedFromMain = false;
    QString m_currentAccountKey;
    QString m_pendingReservationFocusChargerId;
    QSet<QString> m_reservationCancellationLockedAccounts;
    QString m_newUserNicknamePattern;
    QHash<QString, DemoUserData> m_demoUsers;
    QSet<QString> m_failedOnce;
};
