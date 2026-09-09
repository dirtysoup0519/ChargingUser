#pragma once

#include "app/iuseruibinder.h"

class IUserService;
class IAppFlowCoordinator;

class UserUiBinder final : public IUserUiBinder
{
    Q_OBJECT

public:
    UserUiBinder(IUserService *userService,
                 IAppFlowCoordinator *flowCoordinator,
                 QObject *parent = nullptr);

    LoginViewState currentLoginViewState() const override;
    ProfileEditViewState currentProfileEditViewState() const override;
    ProfileViewState currentProfileViewState() const override;

public slots:
    void loginRequested(const QString &phone) override;
    void usernamePasswordLoginRequested(const QString &username,
                                        const QString &password) override;
    void profileSaveRequested(const QString &nickname) override;
    void retryRequested() override;
    void logoutRequested() override;

private slots:
    void handleFlowChanged(const UserFlowSnapshot &snapshot);
    void handleOperationStatusChanged(const UserOperationStatus &status);

private:
    static SubmitState submitStateForError(const ClientError &error);
    void rebuildViewStates(const UserFlowSnapshot &snapshot);
    void publishLoginState(const LoginViewState &state);
    void publishProfileState(const ProfileEditViewState &state);
    void publishProfileSummaryState(const ProfileViewState &state);

    IUserService *m_userService;
    IAppFlowCoordinator *m_flowCoordinator;
    LoginViewState m_loginState;
    ProfileEditViewState m_profileState;
    ProfileViewState m_profileSummaryState;
};
