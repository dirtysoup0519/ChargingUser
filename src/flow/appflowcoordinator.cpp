#include "flow/appflowcoordinator.h"

#include "modules/user/iuserservice.h"

#include <QMetaType>

AppFlowCoordinator::AppFlowCoordinator(IUserService *userService, QObject *parent)
    : IAppFlowCoordinator(parent)
    , m_userService(userService)
{
    Q_ASSERT(m_userService);

    connect(m_userService, &IUserService::loginSucceeded,
            this, &AppFlowCoordinator::handleLoginSucceeded);
    connect(m_userService, &IUserService::currentUserRefreshed,
            this, &AppFlowCoordinator::handleCurrentUserRefreshed);
    connect(m_userService, &IUserService::nicknameUpdated,
            this, &AppFlowCoordinator::handleNicknameUpdated);
    connect(m_userService, &IUserService::logoutSucceeded,
            this, &AppFlowCoordinator::handleLogoutSucceeded);
    connect(m_userService, &IUserService::operationFailed,
            this, &AppFlowCoordinator::handleOperationFailed);
    connect(m_userService, &IUserService::sessionChanged,
            this, &AppFlowCoordinator::handleSessionChanged);
}

UserFlowSnapshot AppFlowCoordinator::currentFlow() const
{
    return m_snapshot;
}

void AppFlowCoordinator::publishFlow()
{
    emit flowChanged(m_snapshot);
}

/* 进入资料完善场景：新用户默认昵称成功/普通失败/资料恢复完成后的统一落点 */
void AppFlowCoordinator::enterProfileRequired(const QString &draft)
{
    m_profileRequired = true;
    m_snapshot.state = UserFlowState::ProfileRequired;
    m_snapshot.target = NavigationTarget::ProfileEdit;
    m_snapshot.draftNickname = draft;   // 失败/恢复场景保留草稿供用户续编
    m_snapshot.error = ClientError{};
    publishFlow();
    emit navigationRequested(NavigationTarget::ProfileEdit);
}

/* 按账号状态分流：Normal → Home，Frozen/Unknown → RestrictedHome（规格 §4） */
void AppFlowCoordinator::finishByAccountStatus()
{
    if (m_snapshot.session.accountStatus == AccountStatus::Normal) {
        m_snapshot.state = UserFlowState::Ready;
        m_snapshot.target = NavigationTarget::Home;
    } else {
        m_snapshot.state = UserFlowState::Restricted;
        m_snapshot.target = NavigationTarget::RestrictedHome;
    }
    // 保存成功即落定：清空草稿与错误
    m_snapshot.draftNickname.clear();
    m_snapshot.error = ClientError{};
    m_profileRequired = false;
    m_retryable = Retryable::None;
    publishFlow();
    emit navigationRequested(m_snapshot.target);
}

/* 非资料完善场景的保存成功：只更新快照，不强制导航（规格 §5）。
 * 按账号状态落定：Frozen/Unknown 用户保存成功后仍是 Restricted，不得错变 Ready（评审 P1-3） */
void AppFlowCoordinator::settleReadyWithoutNavigation()
{
    if (m_snapshot.session.accountStatus == AccountStatus::Normal) {
        m_snapshot.state = UserFlowState::Ready;
    } else {
        m_snapshot.state = UserFlowState::Restricted;
    }
    m_snapshot.error = ClientError{};
    m_snapshot.draftNickname.clear();   // 保存已确认成功，草稿随之清除
    m_profileRequired = false;
    m_retryable = Retryable::None;
    publishFlow();
}

/* 判断失败是否属于昵称修改操作：结果未知标记由服务层精确标记（合同 §12.1） */
bool AppFlowCoordinator::isNicknameOperation(const ClientError &error) const
{
    return error.resultUnknown
           && m_userService->operationStatus(UserOperation::UpdateNickname).state
                  == UserOperationState::ResultUnknown;
}

/* ===== 用户意图入口 ===== */

void AppFlowCoordinator::login(const QString &phone)
{
    // 新的登录生命周期：清空上一账号的会话/错误/草稿与迟到响应隔离标记
    // （规格：换号登录不保留旧账号 session、错误、昵称草稿）
    m_terminated = false;
    m_newUserFlow = false;
    m_profileRequired = false;
    m_pendingConfirmNickname.clear();
    m_retryable = Retryable::None;
    m_phone = phone;
    m_credentialLogin = false;

    m_snapshot = UserFlowSnapshot{};
    m_snapshot.state = UserFlowState::LoggingIn;
    publishFlow();

    // Logout 仍在途时服务层会防重拒绝登录（request-in-progress），
    // 排队本次登录，等退出终态后自动执行，避免页面永久卡在 LoggingIn（评审 P1-2）
    if (m_userService->operationStatus(UserOperation::Logout).state
        == UserOperationState::Running) {
        m_pendingLoginPhone = phone;
        return;
    }
    m_userService->loginByPhone(phone);
}

void AppFlowCoordinator::loginByCredentials(const QString &username,
                                            const QString &password)
{
    m_terminated = false;
    m_newUserFlow = false;
    m_profileRequired = false;
    m_pendingConfirmNickname.clear();
    m_retryable = Retryable::None;
    m_phone.clear();
    m_credentialLogin = true;
    m_snapshot = UserFlowSnapshot{};
    m_snapshot.state = UserFlowState::LoggingIn;
    publishFlow();
    m_userService->loginByCredentials(username, password);
}

/* 排队登录的执行入口：Logout 已到终态，重新走 login() 统一生命周期 */
void AppFlowCoordinator::startPendingLogin()
{
    const QString phone = m_pendingLoginPhone;
    m_pendingLoginPhone.clear();
    if (!phone.isEmpty()) {
        login(phone);
    }
}

void AppFlowCoordinator::saveNickname(const QString &nickname)
{
    if (m_terminated) {
        return;
    }
    // 结果未知恢复期间禁止直接重发修改，必须走资料刷新确认（规格 §5/§7）
    if (m_snapshot.state == UserFlowState::RecoveringProfileUpdate) {
        return;
    }
    // 仅允许编辑中的状态接收保存（验收重点 3 / 评审意见 4）：
    //  - LoggingIn/InitializingNewUser 等阶段调用会被服务层防重拒绝，丢失用户输入；
    //  - 登录失败（未认证）与资料刷新失败（retryable 记录在案）产生的 Error 不发送请求；
    //  - 结果未知恢复确认不一致的 Error（retryable == None 且已认证）仍允许显式保存修正。
    switch (m_snapshot.state) {
    case UserFlowState::ProfileRequired:
    case UserFlowState::Ready:
    case UserFlowState::Restricted:
        break;
    case UserFlowState::Error:
        if (m_retryable != Retryable::None || !m_snapshot.session.authenticated) {
            return;
        }
        break;
    default:
        return;
    }
    m_snapshot.draftNickname = nickname;
    publishFlow();
    m_userService->updateNickname(nickname);
}

void AppFlowCoordinator::retry()
{
    if (m_terminated || m_retryable == Retryable::None) {
        return;   // 昵称保存失败等场景不允许 retry，等待用户显式 saveNickname
    }

    if (m_retryable == Retryable::Login) {
        m_snapshot.state = UserFlowState::LoggingIn;
        m_snapshot.error = ClientError{};
        publishFlow();
        m_userService->loginByPhone(m_phone);
        return;
    }

    // 资料刷新失败与恢复期间刷新失败都走重新刷新；
    // 状态分别回到 RefreshingProfile / 保持 RecoveringProfileUpdate
    if (m_retryable == Retryable::ProfileRefresh) {
        m_snapshot.state = UserFlowState::RefreshingProfile;
    }
    m_snapshot.error = ClientError{};
    publishFlow();
    m_userService->refreshCurrentUser();
}

void AppFlowCoordinator::logout()
{
    // 立即终止本账号流程：此后一切迟到成功/失败响应一律丢弃（规格 §6）
    m_terminated = true;
    m_newUserFlow = false;
    m_profileRequired = false;
    m_pendingConfirmNickname.clear();
    m_retryable = Retryable::None;
    m_phone.clear();
    m_pendingLoginPhone.clear();   // 退出会终止排队中的登录（评审 P1-2）
    m_credentialLogin = false;

    m_snapshot = UserFlowSnapshot{};   // 回到 SignedOut + Login 起点
    publishFlow();
    emit navigationRequested(NavigationTarget::Login);
    m_userService->logout();   // 尽力通知，不等待应答（合同 §3）
}

/* ===== 服务层响应处理 ===== */

void AppFlowCoordinator::handleLoginSucceeded(const LoginResult &result)
{
    if (m_terminated) {
        return;   // 迟到响应：不得恢复旧会话或改变流程
    }
    m_snapshot.session = result.session;
    m_snapshot.error = ClientError{};

    if (result.isNewUser) {
        // 新用户：提交默认昵称"用户+手机号后四位"（合同 §11.2 R8 的编排层落点）。
        // 手机号取自登录应答本身（217 携带 phone），而非 m_phone——
        // 双重登录竞态下 m_phone 可能已被后续登录尝试覆盖（复查修正）。
        m_newUserFlow = true;
        m_profileRequired = true;
        const QString phone = !result.session.profile.phone.isEmpty()
                                  ? result.session.profile.phone
                                  : m_phone;
        const QString defaultNickname = QStringLiteral("用户") + phone.right(4);
        m_snapshot.draftNickname = defaultNickname;
        m_snapshot.state = UserFlowState::InitializingNewUser;
        publishFlow();
        m_userService->updateNickname(defaultNickname);
        return;
    }

    // 老用户：登录后立即刷新资料，按账号状态分流
    m_snapshot.state = UserFlowState::RefreshingProfile;
    publishFlow();
    m_userService->refreshCurrentUser();
}

void AppFlowCoordinator::handleCurrentUserRefreshed(const UserProfileResult &result)
{
    if (m_terminated) {
        return;
    }
    // 以服务层会话为准：服务层已做串号防御与 authenticated 保持
    m_snapshot.session = m_userService->currentSession();

    if (m_snapshot.state == UserFlowState::RecoveringProfileUpdate) {
        m_retryable = Retryable::None;
        if (m_newUserFlow) {
            // 新用户：资料恢复完成即进资料完善（规格 §5），无论比对结果
            enterProfileRequired(result.profile.nickname);
            return;
        }
        if (result.profile.nickname == m_pendingConfirmNickname) {
            // 服务端昵称与预期一致 → 视为保存成功
            m_pendingConfirmNickname.clear();
            if (m_profileRequired) {
                finishByAccountStatus();
            } else {
                settleReadyWithoutNavigation();
            }
        } else {
            // 不一致：说明此前修改未生效且产生了意外变更 → Error，保留草稿，不自动重发
            m_snapshot.state = UserFlowState::Error;
            publishFlow();
        }
        return;
    }

    if (m_snapshot.state == UserFlowState::InitializingNewUser) {
        // 新用户默认昵称恢复完成：进入资料完善，草稿取服务端当前昵称
        m_retryable = Retryable::None;
        enterProfileRequired(result.profile.nickname);
        return;
    }

    if (m_snapshot.state == UserFlowState::RefreshingProfile) {
        // 老用户登录流程的资料刷新：按账号状态分流
        finishByAccountStatus();
        return;
    }

    // ProfileRequired 等场景：只更新快照，不擅自跳转（规格 §3）
    publishFlow();
}

void AppFlowCoordinator::handleNicknameUpdated(const UserProfileResult &result)
{
    if (m_terminated) {
        return;
    }
    m_snapshot.session = m_userService->currentSession();

    if (m_snapshot.state == UserFlowState::InitializingNewUser) {
        // 新用户默认昵称提交成功 → 资料完善
        m_retryable = Retryable::None;
        enterProfileRequired(result.profile.nickname);
        return;
    }

    if (m_snapshot.state == UserFlowState::ProfileRequired || m_profileRequired) {
        // 资料完善场景保存成功 → 按账号状态分流（Normal→Home，否则 RestrictedHome）
        finishByAccountStatus();
        return;
    }

    // 非资料完善场景（如 Ready 用户改名）：只更新快照，不强制导航
    settleReadyWithoutNavigation();
}

void AppFlowCoordinator::handleLogoutSucceeded(const OperationResult &result)
{
    Q_UNUSED(result);
    // 有排队登录时退出已到终态：立即执行（评审 P1-2）
    if (!m_pendingLoginPhone.isEmpty()) {
        startPendingLogin();
        return;
    }
    // 退出成功由 logout() 本地快照先行；此处仅防止服务层确认改变已终止的流程。
    // m_terminated 时一切忽略；理论上仅在未终止时收到（logout 只由本类发起）。
    if (m_terminated) {
        return;
    }
}

void AppFlowCoordinator::handleOperationFailed(const ClientError &error)
{
    // 排队登录：Logout 已到终态（成功走 handleLogoutSucceeded，失败走这里）即执行。
    // 该错误属于退出阶段，不携带进新登录流程（评审 P1-2）。
    if (!m_pendingLoginPhone.isEmpty()
        && m_userService->operationStatus(UserOperation::Logout).state
               != UserOperationState::Running) {
        startPendingLogin();
        return;
    }

    if (m_terminated || m_snapshot.state == UserFlowState::SignedOut) {
        return;   // 迟到响应或未登录状态下的失败不影响流程
    }
    if (error.code == QStringLiteral("request-in-progress")) {
        return;   // 同类请求防重拦截属于 UI 输入抖动，不是流程错误
    }
    m_snapshot.error = error;
    m_snapshot.session = m_userService->currentSession();

    if (isNicknameOperation(error)) {
        // 昵称修改结果未知：禁止直接重发，转资料刷新确认（规格 §5）
        m_pendingConfirmNickname = m_snapshot.draftNickname;
        m_snapshot.state = UserFlowState::RecoveringProfileUpdate;
        m_retryable = Retryable::None;
        publishFlow();
        m_userService->refreshCurrentUser();
        return;
    }

    if (m_snapshot.state == UserFlowState::LoggingIn) {
        // 登录失败：可 retry()（规格 §7）
        m_retryable = m_credentialLogin ? Retryable::None : Retryable::Login;
        m_snapshot.state = UserFlowState::Error;
        m_snapshot.target = NavigationTarget::Login;
        publishFlow();
        return;
    }

    if (m_snapshot.state == UserFlowState::RefreshingProfile) {
        // 老用户资料刷新失败：可 retry()
        m_retryable = Retryable::ProfileRefresh;
        m_snapshot.state = UserFlowState::Error;
        publishFlow();
        return;
    }

    if (m_snapshot.state == UserFlowState::RecoveringProfileUpdate) {
        // 恢复期间的资料刷新失败：仍是"资料刷新失败"，retry() 重新刷新
        m_retryable = Retryable::RecoveryRefresh;
        publishFlow();
        return;
    }

    if (m_snapshot.state == UserFlowState::InitializingNewUser) {
        // 默认昵称普通失败：保留登录会话与默认昵称草稿，进入资料完善（规格 §5）
        m_retryable = Retryable::None;
        enterProfileRequired(m_snapshot.draftNickname);
        return;
    }

    // 普通昵称保存失败：保留草稿与当前状态，等待用户再次显式 saveNickname（规格 §7）
    publishFlow();
}

void AppFlowCoordinator::handleSessionChanged(const UserSession &session)
{
    if (m_terminated) {
        return;   // 迟到的会话变更（如退出清理）不得恢复流程
    }
    m_snapshot.session = session;
    if (m_snapshot.state == UserFlowState::ProfileRequired) {
        // ProfileRequired 场景的资料刷新只更新快照，不擅自跳转（规格 §3）
        publishFlow();
    }
}
