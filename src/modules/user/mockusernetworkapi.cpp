#include "modules/user/mockusernetworkapi.h"

#include <QTimer>

namespace {

ClientError makeError(const MockUserNetworkApi::Behavior &behavior,
                      const RequestContext &context)
{
    ClientError error = behavior.error;
    error.requestId = context.requestId;
    error.operationId = context.operationId;

    if (behavior.outcome == MockUserNetworkApi::Outcome::ResultUnknown) {
        error.code = QStringLiteral("result-unknown");
        error.displayMessage = QStringLiteral("Operation result is unknown.");
        error.retryable = false;
        error.resultUnknown = true;
    } else {
        if (error.code.isEmpty()) {
            error.code = QStringLiteral("mock-failure");
        }
        if (error.displayMessage.isEmpty()) {
            error.displayMessage = QStringLiteral("Mock request failed.");
        }
    }

    return error;
}

} // namespace

MockUserNetworkApi::MockUserNetworkApi(QObject *parent)
    : IUserNetworkApi(parent)
{
}

void MockUserNetworkApi::setLoginBehavior(const Behavior &behavior)
{
    m_loginBehavior = behavior;
}

void MockUserNetworkApi::setQueryBehavior(const Behavior &behavior)
{
    m_queryBehavior = behavior;
}

void MockUserNetworkApi::setNicknameBehavior(const Behavior &behavior)
{
    m_nicknameBehavior = behavior;
}

void MockUserNetworkApi::setLogoutBehavior(const Behavior &behavior)
{
    m_logoutBehavior = behavior;
}

void MockUserNetworkApi::setLoginResult(const LoginResult &result)
{
    m_loginResult = result;
}

void MockUserNetworkApi::setUserProfileResult(const UserProfileResult &result)
{
    m_userProfileResult = result;
}

QString MockUserNetworkApi::lastLoginPhone() const
{
    return m_lastLoginPhone;
}

QString MockUserNetworkApi::lastNickname() const
{
    return m_lastNickname;
}

int MockUserNetworkApi::loginRequestCount() const
{
    return m_loginRequestCount;
}

int MockUserNetworkApi::queryRequestCount() const
{
    return m_queryRequestCount;
}

int MockUserNetworkApi::nicknameRequestCount() const
{
    return m_nicknameRequestCount;
}

int MockUserNetworkApi::logoutRequestCount() const
{
    return m_logoutRequestCount;
}

void MockUserNetworkApi::loginByPhone(const QString &phone, const RequestContext &context)
{
    ++m_loginRequestCount;
    m_lastLoginPhone = phone;

    if (m_loginBehavior.outcome != Outcome::Success) {
        scheduleFailure(m_loginBehavior, context);
        return;
    }

    LoginResult result = m_loginResult;
    result.requestId = context.requestId;
    QTimer::singleShot(m_loginBehavior.delayMs, this, [this, result] {
        emit loginSucceeded(result);
    });
}

void MockUserNetworkApi::queryCurrentUser(const QString &userId,
                                          const RequestContext &context)
{
    Q_UNUSED(userId)
    ++m_queryRequestCount;

    if (m_queryBehavior.outcome != Outcome::Success) {
        scheduleFailure(m_queryBehavior, context);
        return;
    }

    UserProfileResult result = m_userProfileResult;
    result.requestId = context.requestId;
    result.operationId.clear();
    QTimer::singleShot(m_queryBehavior.delayMs, this, [this, result] {
        emit currentUserQuerySucceeded(result);
    });
}

void MockUserNetworkApi::updateNickname(const QString &userId,
                                        const QString &nickname,
                                        const RequestContext &context)
{
    Q_UNUSED(userId)
    ++m_nicknameRequestCount;
    m_lastNickname = nickname;

    if (m_nicknameBehavior.outcome != Outcome::Success) {
        scheduleFailure(m_nicknameBehavior, context);
        return;
    }

    UserProfileResult result = m_userProfileResult;
    result.requestId = context.requestId;
    result.operationId = context.operationId;
    result.profile.nickname = nickname;
    QTimer::singleShot(m_nicknameBehavior.delayMs, this, [this, result] {
        emit nicknameUpdateSucceeded(result);
    });
}

void MockUserNetworkApi::logout(const RequestContext &context)
{
    ++m_logoutRequestCount;

    if (m_logoutBehavior.outcome != Outcome::Success) {
        scheduleFailure(m_logoutBehavior, context);
        return;
    }

    OperationResult result;
    result.requestId = context.requestId;
    result.operationId = context.operationId;
    QTimer::singleShot(m_logoutBehavior.delayMs, this, [this, result] {
        emit logoutSucceeded(result);
    });
}

void MockUserNetworkApi::scheduleFailure(const Behavior &behavior,
                                         const RequestContext &context)
{
    const ClientError error = makeError(behavior, context);
    QTimer::singleShot(behavior.delayMs, this, [this, error] {
        emit requestFailed(error);
    });
}
