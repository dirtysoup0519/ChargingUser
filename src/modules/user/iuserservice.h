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
    virtual void loginByCredentials(const QString &, const QString &) {}
    virtual void refreshCurrentUser() = 0;
    virtual void updateNickname(const QString &nickname) = 0;
    virtual void changePassword(const QString &oldPassword,
                                const QString &newPassword) = 0;
    virtual void logout() = 0;

    virtual UserSession currentSession() const = 0;

    /* 查询操作状态（第一步规格 §二.2）：每次真实异步操作有唯一 Running，
     * 结束后回到 Idle 或 ResultUnknown；未开始过的操作返回 Idle */
    virtual UserOperationStatus operationStatus(UserOperation operation) const = 0;

signals:
    void loginSucceeded(const LoginResult &result);
    void currentUserRefreshed(const UserProfileResult &result);
    void nicknameUpdated(const UserProfileResult &result);
    void passwordChanged(const OperationResult &result);
    void passwordChangeFailed(const ClientError &error);
    void logoutSucceeded(const OperationResult &result);
    void operationFailed(const ClientError &error);
    void sessionChanged(const UserSession &session);
    void operationStatusChanged(const UserOperationStatus &status);
};
