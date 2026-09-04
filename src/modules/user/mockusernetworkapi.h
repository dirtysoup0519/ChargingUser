#pragma once

#include "modules/user/iusernetworkapi.h"

class MockUserNetworkApi final : public IUserNetworkApi
{
    Q_OBJECT

public:
    enum class Outcome
    {
        Success,
        Failure,
        ResultUnknown
    };
    Q_ENUM(Outcome)

    struct Behavior
    {
        Outcome outcome = Outcome::Success;
        int delayMs = 0;
        ClientError error;
    };

    explicit MockUserNetworkApi(QObject *parent = nullptr);

    void setLoginBehavior(const Behavior &behavior);
    void setQueryBehavior(const Behavior &behavior);
    void setNicknameBehavior(const Behavior &behavior);
    void setLogoutBehavior(const Behavior &behavior);

    void setLoginResult(const LoginResult &result);
    void setUserProfileResult(const UserProfileResult &result);

    QString lastLoginPhone() const;
    QString lastNickname() const;
    int loginRequestCount() const;
    int queryRequestCount() const;
    int nicknameRequestCount() const;
    int logoutRequestCount() const;

    void loginByPhone(const QString &phone, const RequestContext &context) override;
    void queryCurrentUser(const QString &userId, const RequestContext &context) override;
    void updateNickname(const QString &userId,
                        const QString &nickname,
                        const RequestContext &context) override;
    void logout(const RequestContext &context) override;

private:
    void scheduleFailure(const Behavior &behavior, const RequestContext &context);

    Behavior m_loginBehavior;
    Behavior m_queryBehavior;
    Behavior m_nicknameBehavior;
    Behavior m_logoutBehavior;
    LoginResult m_loginResult;
    UserProfileResult m_userProfileResult;
    QString m_lastLoginPhone;
    QString m_lastNickname;
    int m_loginRequestCount = 0;
    int m_queryRequestCount = 0;
    int m_nicknameRequestCount = 0;
    int m_logoutRequestCount = 0;
};
