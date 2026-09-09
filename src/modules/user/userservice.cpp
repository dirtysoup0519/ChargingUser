#include "modules/user/userservice.h"

#include "modules/user/iusernetworkapi.h"

#include <QUuid>

UserService::UserService(IUserNetworkApi *networkApi, QObject *parent)
    : IUserService(parent)
    , m_networkApi(networkApi)
{
    Q_ASSERT(m_networkApi);

    connect(m_networkApi, &IUserNetworkApi::loginSucceeded,
            this, &UserService::handleLoginSucceeded);
    connect(m_networkApi, &IUserNetworkApi::currentUserQuerySucceeded,
            this, &UserService::handleCurrentUserQuerySucceeded);
    connect(m_networkApi, &IUserNetworkApi::nicknameUpdateSucceeded,
            this, &UserService::handleNicknameUpdateSucceeded);
    connect(m_networkApi, &IUserNetworkApi::avatarUpdateSucceeded,
            this, &UserService::handleAvatarUpdateSucceeded);
    connect(m_networkApi, &IUserNetworkApi::passwordChangeSucceeded,
            this, &UserService::handlePasswordChangeSucceeded);
    connect(m_networkApi, &IUserNetworkApi::logoutSucceeded,
            this, &UserService::handleLogoutSucceeded);
    connect(m_networkApi, &IUserNetworkApi::requestFailed,
            this, &UserService::handleRequestFailed);
}

void UserService::loginByPhone(const QString &phone)
{
    if (!isPhoneValid(phone)) {
        failLocal(QStringLiteral("invalid-phone"),
                  QStringLiteral("Please enter a valid 11-digit phone number."));
        return;
    }
    if (hasPendingRequest(RequestKind::Login)) {
        failLocal(QStringLiteral("request-in-progress"),
                  QStringLiteral("Login is already in progress."));
        return;
    }
    if (hasPendingRequest(RequestKind::Logout)) {
        failLocal(QStringLiteral("request-in-progress"),
                  QStringLiteral("Logout is already in progress."));
        return;
    }

    const RequestContext context = createContext(true);
    m_pendingRequests.insert(context.requestId,
                             {RequestKind::Login, m_sessionGeneration, context});
    publishOperationState(RequestKind::Login, UserOperationState::Running, context);
    m_networkApi->loginByPhone(phone, context);
}

void UserService::loginByCredentials(const QString &username,
                                     const QString &password)
{
    if (username.trimmed().isEmpty() || password.isEmpty()) {
        failLocal(QStringLiteral("invalid-credentials"),
                  QStringLiteral("Username and password are required."));
        return;
    }
    if (hasPendingRequest(RequestKind::Login)) {
        failLocal(QStringLiteral("request-in-progress"),
                  QStringLiteral("Login is already in progress."));
        return;
    }
    if (hasPendingRequest(RequestKind::Logout)) {
        failLocal(QStringLiteral("request-in-progress"),
                  QStringLiteral("Logout is still in progress. Please try again."));
        return;
    }
    const RequestContext context = createContext(true);
    m_pendingRequests.insert(context.requestId,
                             {RequestKind::Login, m_sessionGeneration, context});
    publishOperationState(RequestKind::Login, UserOperationState::Running, context);
    m_networkApi->loginByCredentials(username.trimmed(), password, context);
}

void UserService::refreshCurrentUser()
{
    if (!m_session.authenticated || m_session.profile.userId.isEmpty()) {
        failLocal(QStringLiteral("not-authenticated"),
                  QStringLiteral("No active user session."));
        return;
    }
    if (hasPendingRequest(RequestKind::QueryCurrentUser)) {
        failLocal(QStringLiteral("request-in-progress"),
                  QStringLiteral("Profile refresh is already in progress."));
        return;
    }

    const RequestContext context = createContext(false);
    m_pendingRequests.insert(context.requestId,
                             {RequestKind::QueryCurrentUser, m_sessionGeneration, context});
    publishOperationState(RequestKind::QueryCurrentUser,
                          UserOperationState::Running, context);
    m_networkApi->queryCurrentUser(m_session.profile.userId, context);
}

void UserService::updateNickname(const QString &nickname)
{
    if (!m_session.authenticated || m_session.profile.userId.isEmpty()) {
        failLocal(QStringLiteral("not-authenticated"),
                  QStringLiteral("No active user session."));
        return;
    }
    // 昵称校验（第一步规格 §二.3）：先裁剪首尾空白，再细分校验错误码
    const QString trimmed = nickname.trimmed();
    if (!isNicknameValid(trimmed)) {
        if (trimmed.isEmpty()) {
            failLocal(QStringLiteral("invalid-nickname-empty"),
                      QStringLiteral("Nickname cannot be empty."));
        } else if (trimmed.size() > 20) {
            failLocal(QStringLiteral("invalid-nickname-too-long"),
                      QStringLiteral("Nickname must not exceed 20 characters."));
        } else {
            failLocal(QStringLiteral("invalid-nickname-control-character"),
                      QStringLiteral("Nickname must not contain control characters."));
        }
        return;
    }
    if (hasPendingRequest(RequestKind::UpdateNickname)
        || hasPendingRequest(RequestKind::UpdateAvatar)
        || hasPendingRequest(RequestKind::ChangePassword)) {
        failLocal(QStringLiteral("request-in-progress"),
                  QStringLiteral("Profile update is already in progress."));
        return;
    }
    if (m_profileUpdateResultUnknown) {
        failLocal(QStringLiteral("result-unknown-pending"),
                  QStringLiteral("Previous profile update requires result recovery."));
        return;
    }

    const RequestContext context = createContext(true);
    m_pendingRequests.insert(context.requestId,
                             {RequestKind::UpdateNickname, m_sessionGeneration, context});
    publishOperationState(RequestKind::UpdateNickname,
                          UserOperationState::Running, context);
    m_networkApi->updateNickname(m_session.profile.userId, trimmed, context);
}

void UserService::updateAvatar(const QString &avatarDataUri)
{
    if (!m_session.authenticated || m_session.profile.userId.isEmpty()) {
        failLocal(QStringLiteral("not-authenticated"),
                  QStringLiteral("No active user session."));
        return;
    }
    if (!avatarDataUri.startsWith(QStringLiteral("data:image/"))
        || avatarDataUri.toUtf8().size() > 96 * 1024) {
        failLocal(QStringLiteral("invalid-avatar"),
                  QStringLiteral("Avatar must be an image data URI no larger than 96 KB."));
        return;
    }
    if (hasPendingRequest(RequestKind::UpdateNickname)
        || hasPendingRequest(RequestKind::UpdateAvatar)
        || hasPendingRequest(RequestKind::ChangePassword)) {
        failLocal(QStringLiteral("request-in-progress"),
                  QStringLiteral("Profile update is already in progress."));
        return;
    }
    if (m_profileUpdateResultUnknown) {
        failLocal(QStringLiteral("result-unknown-pending"),
                  QStringLiteral("Previous profile update requires result recovery."));
        return;
    }
    const RequestContext context = createContext(true);
    m_pendingRequests.insert(context.requestId,
                             {RequestKind::UpdateAvatar, m_sessionGeneration, context});
    publishOperationState(RequestKind::UpdateAvatar,
                          UserOperationState::Running, context);
    m_networkApi->updateAvatar(m_session.profile.userId, avatarDataUri, context);
}

void UserService::changePassword(const QString &oldPassword,
                                 const QString &newPassword)
{
    if (!m_session.authenticated || m_session.profile.userId.isEmpty()) {
        failLocal(QStringLiteral("not-authenticated"),
                  QStringLiteral("No active user session."));
        return;
    }
    if (newPassword.size() < 6) {
        failLocal(QStringLiteral("invalid-new-password"),
                  QStringLiteral("New password must contain at least 6 characters."));
        return;
    }
    if (hasPendingRequest(RequestKind::ChangePassword)
        || hasPendingRequest(RequestKind::UpdateAvatar)
        || hasPendingRequest(RequestKind::UpdateNickname)) {
        failLocal(QStringLiteral("request-in-progress"),
                  QStringLiteral("A profile change is already in progress."));
        return;
    }
    if (m_passwordChangeResultUnknown) {
        failLocal(QStringLiteral("result-unknown-pending"),
                  QStringLiteral("Previous password change result is unknown. Please sign in again to verify it."));
        return;
    }

    const RequestContext context = createContext(true);
    m_pendingRequests.insert(context.requestId,
                             {RequestKind::ChangePassword,
                              m_sessionGeneration, context});
    publishOperationState(RequestKind::ChangePassword,
                          UserOperationState::Running, context);
    m_networkApi->changePassword(m_session.profile.userId, oldPassword,
                                 newPassword, context);
}

void UserService::logout()
{
    if (hasPendingRequest(RequestKind::Logout)) {
        failLocal(QStringLiteral("request-in-progress"),
                  QStringLiteral("Logout is already in progress."));
        return;
    }

    clearSession();
    const RequestContext context = createContext(true);
    m_pendingRequests.insert(context.requestId,
                             {RequestKind::Logout, m_sessionGeneration, context});
    publishOperationState(RequestKind::Logout, UserOperationState::Running, context);
    m_networkApi->logout(context);
}

UserSession UserService::currentSession() const
{
    return m_session;
}

UserOperationStatus UserService::operationStatus(UserOperation operation) const
{
    const auto iterator = m_operationStates.constFind(static_cast<int>(operation));
    if (iterator != m_operationStates.constEnd()) {
        return iterator.value();
    }
    UserOperationStatus status;
    status.operation = operation;
    return status;
}

void UserService::handleLoginSucceeded(const LoginResult &result)
{
    PendingRequest pending;
    if (!takePendingRequest(result.requestId, RequestKind::Login, &pending)) {
        return;
    }

    // 换号重登不经过 logout：必须提升会话世代并清空旧在途请求，
    // 否则旧账号的迟到应答（同世代）会覆盖刚建立的会话（审查问题 3）
    ++m_sessionGeneration;
    m_pendingRequests.clear();
    // 新会话不能继承旧账号的操作状态或请求上下文。publishOperationState
    // 会抑制默认 Idle 的无效广播，因此首次普通登录仍只有 Login 两次迁移。
    publishOperationState(RequestKind::QueryCurrentUser, UserOperationState::Idle);
    publishOperationState(RequestKind::UpdateNickname, UserOperationState::Idle);
    publishOperationState(RequestKind::UpdateAvatar, UserOperationState::Idle);
    publishOperationState(RequestKind::ChangePassword, UserOperationState::Idle);
    publishOperationState(RequestKind::Logout, UserOperationState::Idle);

    LoginResult confirmed = result;
    confirmed.session.authenticated = true;
    m_session = confirmed.session;
    m_profileUpdateResultUnknown = false;
    m_passwordChangeResultUnknown = false;
    publishOperationState(RequestKind::Login, UserOperationState::Idle, pending.context);
    emit sessionChanged(m_session);
    emit loginSucceeded(confirmed);
}

void UserService::handleCurrentUserQuerySucceeded(const UserProfileResult &result)
{
    PendingRequest pending;
    if (!takePendingRequest(result.requestId, RequestKind::QueryCurrentUser, &pending)
        || pending.sessionGeneration != m_sessionGeneration) {
        return;
    }

    // 防串号（第一步规格 §二.4）：应答用户与当前会话不符时不合并会话、
    // 不解除结果未知，操作回到 Idle；网络层已按 218 回显 username 校验过一次，
    // 这是服务层的第二道防御
    if (result.profile.userId != m_session.profile.userId) {
        ClientError error;
        error.code = QStringLiteral("profile-user-mismatch");
        error.displayMessage =
            QStringLiteral("Profile response does not match the current user.");
        error.requestId = result.requestId;
        error.operationId = result.operationId;
        // 与其他路径统一：先发布终态，再发错误信号
        publishOperationState(RequestKind::QueryCurrentUser,
                              UserOperationState::Idle,
                              RequestContext{result.requestId, result.operationId});
        emit operationFailed(error);
        return;
    }

    m_session.profile = result.profile;
    m_session.accountStatus = result.accountStatus;
    // 查询结果是服务端权威状态：改昵称“结果未知”的锁定在此解除，
    // 用户可依据当前昵称决定是否重试（合同 §11 v1.2 恢复规则，审查问题 4）
    const bool recoveringUnknownUpdate = m_profileUpdateResultUnknown;
    m_profileUpdateResultUnknown = false;
    publishOperationState(RequestKind::QueryCurrentUser,
                          UserOperationState::Idle,
                          RequestContext{result.requestId, result.operationId});
    // 只有结果未知的昵称修改可由权威资料刷新解除。正常的并发昵称请求
    // 仍须等待自身终态，不能被资料刷新提前标记为 Idle。
    if (recoveringUnknownUpdate) {
        publishOperationState(RequestKind::UpdateNickname,
                              UserOperationState::Idle);
        publishOperationState(RequestKind::UpdateAvatar,
                              UserOperationState::Idle);
    }
    emit sessionChanged(m_session);
    emit currentUserRefreshed(result);
}

void UserService::handleNicknameUpdateSucceeded(const UserProfileResult &result)
{
    PendingRequest pending;
    if (!takePendingRequest(result.requestId, RequestKind::UpdateNickname, &pending)
        || pending.sessionGeneration != m_sessionGeneration) {
        return;
    }

    // 219 应答只携带昵称（协议 v2.1），不得用部分资料整体覆盖会话，
    // 也不得把 accountStatus 抹成 Unknown（Unknown 按合同按受限状态处理）
    m_session.profile.nickname = result.profile.nickname;
    publishOperationState(RequestKind::UpdateNickname, UserOperationState::Idle,
                          pending.context);
    emit sessionChanged(m_session);
    emit nicknameUpdated(result);
}

void UserService::handleAvatarUpdateSucceeded(const UserProfileResult &result)
{
    PendingRequest pending;
    if (!takePendingRequest(result.requestId, RequestKind::UpdateAvatar, &pending)
        || pending.sessionGeneration != m_sessionGeneration)
        return;
    m_session.profile.avatarKey = result.profile.avatarKey;
    publishOperationState(RequestKind::UpdateAvatar, UserOperationState::Idle,
                          pending.context);
    emit sessionChanged(m_session);
    emit avatarUpdated(result);
}

void UserService::handlePasswordChangeSucceeded(const OperationResult &result)
{
    PendingRequest pending;
    if (!takePendingRequest(result.requestId, RequestKind::ChangePassword, &pending)
        || pending.sessionGeneration != m_sessionGeneration) {
        return;
    }
    m_passwordChangeResultUnknown = false;
    publishOperationState(RequestKind::ChangePassword, UserOperationState::Idle,
                          pending.context);
    emit passwordChanged(result);
}

void UserService::handleLogoutSucceeded(const OperationResult &result)
{
    PendingRequest pending;
    if (!takePendingRequest(result.requestId, RequestKind::Logout, &pending)
        || pending.sessionGeneration != m_sessionGeneration) {
        return;
    }

    publishOperationState(RequestKind::Logout, UserOperationState::Idle,
                          pending.context);
    emit logoutSucceeded(result);
}

void UserService::handleRequestFailed(const ClientError &error)
{
    const auto iterator = m_pendingRequests.find(error.requestId);
    if (iterator == m_pendingRequests.end()) {
        return;
    }

    const PendingRequest pending = iterator.value();
    m_pendingRequests.erase(iterator);
    if (pending.sessionGeneration != m_sessionGeneration) {
        return;
    }
    if ((pending.kind == RequestKind::UpdateNickname
         || pending.kind == RequestKind::UpdateAvatar
         || pending.kind == RequestKind::ChangePassword)
        && error.resultUnknown) {
        if (pending.kind == RequestKind::UpdateNickname
            || pending.kind == RequestKind::UpdateAvatar)
            m_profileUpdateResultUnknown = true;
        else
            m_passwordChangeResultUnknown = true;
        publishOperationState(pending.kind, UserOperationState::ResultUnknown,
                              RequestContext{error.requestId, error.operationId});
    } else {
        publishOperationState(pending.kind, UserOperationState::Idle,
                              RequestContext{error.requestId, error.operationId});
    }

    if (pending.kind == RequestKind::ChangePassword)
        emit passwordChangeFailed(error);
    emit operationFailed(error);
}

RequestContext UserService::createContext(bool isMutation) const
{
    RequestContext context;
    context.requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (isMutation) {
        context.operationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    return context;
}

bool UserService::isPhoneValid(const QString &phone) const
{
    if (phone.size() != 11 || phone.front() != QLatin1Char('1')) {
        return false;
    }
    for (const QChar character : phone) {
        if (!character.isDigit()) {
            return false;
        }
    }
    return true;
}

bool UserService::isNicknameValid(const QString &trimmedNickname) const
{
    // 规格要求：裁剪后非空、长度 ≤ 20 个 QChar、不含控制字符（含换行/制表符）
    if (trimmedNickname.isEmpty() || trimmedNickname.size() > 20) {
        return false;
    }
    for (const QChar character : trimmedNickname) {
        // QChar::category() 兼容 Qt5/Qt6：Other_Control 覆盖换行、制表符等全部控制字符
        if (character.category() == QChar::Other_Control) {
            return false;
        }
    }
    return true;
}

UserOperation UserService::toUserOperation(RequestKind kind) const
{
    switch (kind) {
    case RequestKind::Login:
        return UserOperation::Login;
    case RequestKind::QueryCurrentUser:
        return UserOperation::RefreshProfile;
    case RequestKind::UpdateNickname:
        return UserOperation::UpdateNickname;
    case RequestKind::UpdateAvatar:
        return UserOperation::UpdateAvatar;
    case RequestKind::ChangePassword:
        return UserOperation::ChangePassword;
    case RequestKind::Logout:
        return UserOperation::Logout;
    }
    return UserOperation::Login;
}

void UserService::publishOperationState(RequestKind kind, UserOperationState state,
                                        const RequestContext &context)
{
    const UserOperation operation = toUserOperation(kind);
    UserOperationStatus status;
    status.operation = operation;
    status.state = state;
    status.requestId = context.requestId;
    status.operationId = context.operationId;

    // 仅在状态或关联上下文真正变化时广播（评审意见 P2）：
    // operationStatusChanged 表达真实迁移，避免订阅者无效渲染
    // 未记录过的操作在公开语义上就是 Idle + 空上下文，不能把第一次
    // “复位”误报为状态变化。
    const UserOperationStatus current = operationStatus(operation);
    if (current.state == status.state
        && current.requestId == status.requestId
        && current.operationId == status.operationId) {
        return;
    }

    m_operationStates.insert(static_cast<int>(operation), status);
    emit operationStatusChanged(status);
}

bool UserService::hasPendingRequest(RequestKind kind) const
{
    for (const PendingRequest &pending : m_pendingRequests) {
        if (pending.kind == kind && pending.sessionGeneration == m_sessionGeneration) {
            return true;
        }
    }
    return false;
}

bool UserService::takePendingRequest(const QString &requestId,
                                     RequestKind expectedKind,
                                     PendingRequest *pending)
{
    const auto iterator = m_pendingRequests.find(requestId);
    if (iterator == m_pendingRequests.end() || iterator->kind != expectedKind) {
        return false;
    }

    *pending = iterator.value();
    m_pendingRequests.erase(iterator);
    return true;
}

void UserService::failLocal(const QString &code, const QString &message)
{
    ClientError error;
    error.code = code;
    error.displayMessage = message;
    emit operationFailed(error);
}

void UserService::clearSession()
{
    ++m_sessionGeneration;
    m_pendingRequests.clear();
    m_session = UserSession{};
    m_profileUpdateResultUnknown = false;
    m_passwordChangeResultUnknown = false;
    // 会话清理后所有操作回到 Idle（第一步规格 §二.5）
    publishOperationState(RequestKind::Login, UserOperationState::Idle);
    publishOperationState(RequestKind::QueryCurrentUser, UserOperationState::Idle);
    publishOperationState(RequestKind::UpdateNickname, UserOperationState::Idle);
    publishOperationState(RequestKind::UpdateAvatar, UserOperationState::Idle);
    publishOperationState(RequestKind::ChangePassword, UserOperationState::Idle);
    publishOperationState(RequestKind::Logout, UserOperationState::Idle);
    emit sessionChanged(m_session);
}
