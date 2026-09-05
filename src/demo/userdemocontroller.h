#pragma once

#include "flow/userflowtypes.h"

#include <QObject>
#include <QSet>

class IUserUiBinder;
class LoginWindow;
class MainWindow;
class MockUserNetworkApi;
class ProfileEditWindow;
class NavigationWindow;
class StationDetailWindow;
class WalletRechargeWindow;
class QWidget;

/* Demo 专用页面接线器。它只存在于 CONFIG+=user_demo 和 UI 集成测试中，
 * 正式入口不会使用模拟账号或模拟网络结果。
 */
class UserDemoController final : public QObject
{
public:
    UserDemoController(MockUserNetworkApi *network,
                       IUserUiBinder *binder,
                       LoginWindow *login,
                       ProfileEditWindow *profileEdit,
                       MainWindow *mainWindow,
                       QObject *parent = nullptr);

    void showInitialPage();

private:
    void configureLogin(const QString &phone);
    void configureNicknameSave(const QString &nickname);
    void handleNavigation(NavigationTarget target);
    void showOnly(QWidget *target);

    MockUserNetworkApi *m_network;
    IUserUiBinder *m_binder;
    LoginWindow *m_login;
    ProfileEditWindow *m_profileEdit;
    MainWindow *m_mainWindow;
    StationDetailWindow *m_stationDetail;
    NavigationWindow *m_navigation;
    WalletRechargeWindow *m_walletRecharge;
    QSet<QString> m_registeredPhones;
    QSet<QString> m_failedOnce;
};
