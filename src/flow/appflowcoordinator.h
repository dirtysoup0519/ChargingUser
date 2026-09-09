#pragma once

/* AppFlowCoordinator：IAppFlowCoordinator 的默认实现。
 * 状态机（业务规则对应规格条款）：
 *   login()                 LoggingIn
 *   登录成功（老用户）        RefreshingProfile → 刷新成功按账号状态分流
 *   登录成功（新用户）        InitializingNewUser → 默认昵称提交
 *                             ├ 成功 / 普通失败 / 恢复完成 → ProfileRequired + ProfileEdit
 *                             └ ResultUnknown → RecoveringProfileUpdate（刷新确认）
 *   昵称保存 ResultUnknown    RecoveringProfileUpdate（禁止自动重发，只能刷新确认）
 *   刷新确认昵称一致          保存成功：资料完善场景分流 Home/RestrictedHome，否则 Ready
 *   刷新确认昵称不一致        Error（保留草稿，不自动重发）
 *   logout()                SignedOut + Login，此后一切迟到响应丢弃
 * 迟到响应隔离：依靠 m_terminated 标记（退出后）+ UserService 的会话世代过滤。
 */
#include "iappflowcoordinator.h"

#include "common/operationresult.h"

class IUserService;

class AppFlowCoordinator final : public IAppFlowCoordinator
{
    Q_OBJECT

public:
    explicit AppFlowCoordinator(IUserService *userService, QObject *parent = nullptr);

    UserFlowSnapshot currentFlow() const override;

public slots:
    void login(const QString &phone) override;
    void loginByCredentials(const QString &username,
                            const QString &password) override;
    void saveNickname(const QString &nickname) override;
    void retry() override;
    void logout() override;

private slots:
    void handleLoginSucceeded(const LoginResult &result);
    void handleCurrentUserRefreshed(const UserProfileResult &result);
    void handleNicknameUpdated(const UserProfileResult &result);
    void handleLogoutSucceeded(const OperationResult &result);
    void handleOperationFailed(const ClientError &error);
    void handleSessionChanged(const UserSession &session);

private:
    /* 最近一次可重试失败类型：决定 retry() 的行为（规格 §7） */
    enum class Retryable
    {
        None,             // 不可重试（昵称失败等，等待显式 saveNickname）
        Login,            // 登录失败 → 重发 loginByPhone
        ProfileRefresh,   // 老用户资料刷新失败 → 重发 refreshCurrentUser
        RecoveryRefresh   // 结果未知恢复期间的刷新失败 → 重新刷新确认
    };

    void publishFlow();
    void enterProfileRequired(const QString &draft);
    void finishByAccountStatus();
    void settleReadyWithoutNavigation();
    void startPendingLogin();
    bool isNicknameOperation(const ClientError &error) const;

    IUserService *m_userService;
    UserFlowSnapshot m_snapshot;
    QString m_phone;                    // 当前/最近一次登录尝试的手机号（retry 登录用）
    QString m_pendingLoginPhone;        // 排队中的登录：等 Logout 终态后执行（评审 P1-2）
    bool m_newUserFlow = false;         // 本次登录是否为新用户默认昵称编排
    bool m_profileRequired = false;     // 是否处于资料完善场景（决定保存成功后的导航去向）
    QString m_pendingConfirmNickname;   // 结果未知恢复期间待与服务端比对的昵称
    Retryable m_retryable = Retryable::None;
    bool m_terminated = false;          // logout 后置位：一切迟到响应丢弃
    bool m_credentialLogin = false;
};
