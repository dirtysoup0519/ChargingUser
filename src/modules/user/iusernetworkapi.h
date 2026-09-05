#pragma once

#include "common/clienterror.h"
#include "common/operationresult.h"
#include "common/requestcontext.h"
#include "modules/user/usertypes.h"

#include <QObject>
#include <QString>

class IUserNetworkApi : public QObject
{
    Q_OBJECT

public:
    explicit IUserNetworkApi(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~IUserNetworkApi() override = default;

    virtual void loginByPhone(const QString &phone,
                              const RequestContext &context) = 0;
    virtual void queryCurrentUser(const QString &userId,
                                  const RequestContext &context) = 0;
    virtual void updateNickname(const QString &userId,
                                const QString &nickname,
                                const RequestContext &context) = 0;
    virtual void logout(const RequestContext &context) = 0;

signals:
    void loginSucceeded(const LoginResult &result);
    void currentUserQuerySucceeded(const UserProfileResult &result);
    void nicknameUpdateSucceeded(const UserProfileResult &result);
    void logoutSucceeded(const OperationResult &result);
    void requestFailed(const ClientError &error);
};
