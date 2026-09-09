#pragma once

#include "common/requestcontext.h"
#include "modules/user/iuserservice.h"

#include <QHash>

class IUserNetworkApi;

class UserService final : public IUserService
{
    Q_OBJECT

public:
    explicit UserService(IUserNetworkApi *networkApi, QObject *parent = nullptr);

    void loginByPhone(const QString &phone) override;
    void loginByCredentials(const QString &username, const QString &password) override;
    void refreshCurrentUser() override;
    void updateNickname(const QString &nickname) override;
    void updateAvatar(const QString &avatarDataUri) override;
    void changePassword(const QString &oldPassword,
                        const QString &newPassword) override;
    void logout() override;

    UserSession currentSession() const override;
    UserOperationStatus operationStatus(UserOperation operation) const override;

private slots:
    void handleLoginSucceeded(const LoginResult &result);
    void handleCurrentUserQuerySucceeded(const UserProfileResult &result);
    void handleNicknameUpdateSucceeded(const UserProfileResult &result);
    void handleAvatarUpdateSucceeded(const UserProfileResult &result);
    void handlePasswordChangeSucceeded(const OperationResult &result);
    void handleLogoutSucceeded(const OperationResult &result);
    void handleRequestFailed(const ClientError &error);

private:
    enum class RequestKind
    {
        Login,
        QueryCurrentUser,
        UpdateNickname,
        UpdateAvatar,
        ChangePassword,
        Logout
    };

    struct PendingRequest
    {
        RequestKind kind;
        quint64 sessionGeneration = 0;
        RequestContext context;
    };

    RequestContext createContext(bool isMutation) const;
    bool isPhoneValid(const QString &phone) const;
    bool isNicknameValid(const QString &trimmedNickname) const;
    UserOperation toUserOperation(RequestKind kind) const;
    void publishOperationState(RequestKind kind,
                               UserOperationState state,
                               const RequestContext &context = RequestContext());
    bool hasPendingRequest(RequestKind kind) const;
    bool takePendingRequest(const QString &requestId,
                            RequestKind expectedKind,
                            PendingRequest *pending);
    void failLocal(const QString &code, const QString &message);
    void clearSession();

    IUserNetworkApi *m_networkApi = nullptr;
    UserSession m_session;
    QHash<QString, PendingRequest> m_pendingRequests;
    quint64 m_sessionGeneration = 0;
    bool m_profileUpdateResultUnknown = false;
    bool m_passwordChangeResultUnknown = false;
    QHash<int, UserOperationStatus> m_operationStates;
};
