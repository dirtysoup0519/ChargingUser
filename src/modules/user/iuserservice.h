#pragma once

#include "common/clienterror.h"
#include "common/operationresult.h"
#include "modules/user/usertypes.h"

#include <QObject>
#include <QString>

class IUserService : public QObject
{
    Q_OBJECT

public:
    explicit IUserService(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~IUserService() override = default;

    virtual void loginByPhone(const QString &phone) = 0;
    virtual void refreshCurrentUser() = 0;
    virtual void updateNickname(const QString &nickname) = 0;
    virtual void logout() = 0;

    virtual UserSession currentSession() const = 0;

signals:
    void loginSucceeded(const LoginResult &result);
    void currentUserRefreshed(const UserProfileResult &result);
    void nicknameUpdated(const UserProfileResult &result);
    void logoutSucceeded(const OperationResult &result);
    void operationFailed(const ClientError &error);
    void sessionChanged(const UserSession &session);
};
