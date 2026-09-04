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
    m_networkApi->loginByPhone(phone, context);
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
    m_networkApi->queryCurrentUser(m_session.profile.userId, context);
}

void UserService::updateNickname(const QString &nickname)
{
    if (!m_session.authenticated || m_session.profile.userId.isEmpty()) {
        failLocal(QStringLiteral("not-authenticated"),
                  QStringLiteral("No active user session."));
        return;
    }
    if (nickname.trimmed().isEmpty()) {
        failLocal(QStringLiteral("invalid-nickname"),
                  QStringLiteral("Nickname cannot be empty."));
        return;
    }
    if (hasPendingRequest(RequestKind::UpdateNickname)) {
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
    m_networkApi->updateNickname(m_session.profile.userId, nickname.trimmed(), context);
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
    m_networkApi->logout(context);
}

UserSession UserService::currentSession() const
{
    return m_session;
}

void UserService::handleLoginSucceeded(const LoginResult &result)
{
    PendingRequest pending;
    if (!takePendingRequest(result.requestId, RequestKind::Login, &pending)) {
        return;
    }
    if (pending.sessionGeneration != m_sessionGeneration) {
        return;
    }

    LoginResult confirmed = result;
    confirmed.session.authenticated = true;
    m_session = confirmed.session;
    m_profileUpdateResultUnknown = false;
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

    m_session.profile = result.profile;
    m_session.accountStatus = result.accountStatus;
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

    m_session.profile = result.profile;
    m_session.accountStatus = result.accountStatus;
    emit sessionChanged(m_session);
    emit nicknameUpdated(result);
}

void UserService::handleLogoutSucceeded(const OperationResult &result)
{
    PendingRequest pending;
    if (!takePendingRequest(result.requestId, RequestKind::Logout, &pending)
        || pending.sessionGeneration != m_sessionGeneration) {
        return;
    }

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
    if (pending.kind == RequestKind::UpdateNickname && error.resultUnknown) {
        m_profileUpdateResultUnknown = true;
    }

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
    emit sessionChanged(m_session);
}
