#pragma once

#include "app/imapuibinder.h"
#include "flow/userflowtypes.h"
#include "presentation/contracts/chargingviewstates.h"
#include "presentation/contracts/reservationviewstates.h"
#include "presentation/contracts/scanviewstate.h"
#include "presentation/contracts/chargingsessionviewstate.h"
#include "presentation/contracts/paymentviewstates.h"
#include "presentation/contracts/orderlistviewstate.h"
#include "presentation/contracts/orderdetailviewstate.h"
#include "modules/charger/chargertypes.h"

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
class ChargingSessionWindow;
class ReservationConfirmationWindow;
class QrCodeScannerWindow;
class SettlementWindow;
class PaymentWindow;
class OrderListWindow;
class OrderDetailWindow;
class FrequentStationsWindow;
class ProfileTextWindow;
class PasswordChangeWindow;
class QWidget;
struct DemoUserData
{
    QString nickname;
    QString password;
    QString avatarDataUri;
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
    ~UserDemoController() override;

    void showInitialPage();

private:
    void configureLogin(const QString &phone);
    void configureNicknameSave(const QString &nickname);
    void rememberConfirmedLogin(const LoginResult &result);
    void rememberConfirmedNickname(const UserProfileResult &result);
    void rememberConfirmedAvatar(const UserProfileResult &result);
    void handleNavigation(NavigationTarget target);
    void handleMapPage(MapPageTarget target, const QString &stationId);
    void renderStationDetailWithReservation(StationDetailViewState state);
    void renderHomeWithReservation(HomeMapViewState state);
    void showChargeConfirmation(const QString &stationId, const QString &chargerId);
    void renderChargingSessions(const QString &selectedOrderId = QString());
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
    ChargingSessionWindow *m_chargingSession = nullptr;
    ReservationConfirmationWindow *m_reservationConfirmation;
    QrCodeScannerWindow *m_qrScanner;
    SettlementWindow *m_settlement;
    PaymentWindow *m_payment;
    OrderListWindow *m_orderList;
    OrderDetailWindow *m_orderDetail;
    FrequentStationsWindow *m_frequentStations;
    ProfileTextWindow *m_profileText;
    PasswordChangeWindow *m_passwordChange;
    ChargeConfirmationViewState m_chargeConfirmationState;
    QList<ChargingSessionViewState> m_chargingSessionStates;
    QList<ChargingSessionViewState> m_chargingSessionDemoTemplates;
    QVector<StationDetail> m_demoStations;
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
    bool m_usernameFirstSetup = false;
    QString m_pendingUsername;
    QString m_pendingUsernamePassword;
    QString m_pendingAvatarDataUri;
    bool m_scannerOpenedFromCharging = false;
    bool m_scannerOpenedFromOrders = false;
    QString m_currentAccountKey;
    QString m_pendingReservationFocusChargerId;
    QSet<QString> m_reservationCancellationLockedAccounts;
    QSet<QString> m_demoChargingChargerKeys;
    QHash<QString, QString> m_orderChargerKeys;
    QString m_selectedChargingOrderId;
    SettlementViewState m_settlementState;
    PaymentViewState m_paymentState;
    OrderListViewState m_orderListState;
    QString m_paymentBalanceText = QStringLiteral("--");
    QString m_paymentOutcome = QStringLiteral("success");
    int m_paymentDelayMs = 450;
    qint64 m_paymentBalanceCents = 0;
    bool m_paymentOperationActive = false;
    QString m_pendingReservationStationId;
    QString m_pendingReservationChargerId;
    int m_pendingReservationDurationSeconds = 0;
    bool m_walletOpenedFromPayment = false;
    QString m_newUserNicknamePattern;
    QHash<QString, DemoUserData> m_demoUsers;
    QSet<QString> m_failedOnce;
};
