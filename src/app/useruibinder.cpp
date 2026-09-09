#include "app/useruibinder.h"

#include "flow/iappflowcoordinator.h"
#include "modules/user/iuserservice.h"

#include <QMetaType>

namespace
{

bool isNetworkErrorCode(const QString &code)
{
    return code == QStringLiteral("connection-lost")
           || code == QStringLiteral("not-connected")
           || code == QStringLiteral("request-timeout")
           || code == QStringLiteral("send-failed")
           || code.startsWith(QStringLiteral("network-"))
           || code.startsWith(QStringLiteral("transport-"));
}

bool loginStatesEqual(const LoginViewState &left, const LoginViewState &right)
{
    return left.submitState == right.submitState
           && left.phoneInput == right.phoneInput
           && left.message == right.message
           && left.canSubmit == right.canSubmit
           && left.canRetry == right.canRetry;
}

bool profileStatesEqual(const ProfileEditViewState &left,
                        const ProfileEditViewState &right)
{
    return left.submitState == right.submitState
           && left.phone == right.phone
           && left.nicknameInput == right.nicknameInput
           && left.avatarDataUri == right.avatarDataUri
           && left.message == right.message
           && left.canSubmit == right.canSubmit
           && left.canRetry == right.canRetry;
}

bool profileSummaryStatesEqual(const ProfileViewState &left,
                               const ProfileViewState &right)
{
    return left.nickname == right.nickname
           && left.maskedPhone == right.maskedPhone
           && left.balanceText == right.balanceText
           && left.avatarDataUri == right.avatarDataUri
           && left.accountState == right.accountState
           && left.accountMessage == right.accountMessage;
}

QString maskedPhone(const QString &phone)
{
    if (phone.size() != 11) {
        return phone;
    }
    return phone.left(3) + QStringLiteral("****") + phone.right(4);
}

} // namespace

UserUiBinder::UserUiBinder(IUserService *userService,
                           IAppFlowCoordinator *flowCoordinator,
                           QObject *parent)
    : IUserUiBinder(parent)
    , m_userService(userService)
    , m_flowCoordinator(flowCoordinator)
{
    Q_ASSERT(m_userService);
    Q_ASSERT(m_flowCoordinator);

    qRegisterMetaType<SubmitState>();
    qRegisterMetaType<LoginViewState>();
    qRegisterMetaType<ProfileEditViewState>();
    qRegisterMetaType<AccountDisplayState>();
    qRegisterMetaType<ProfileViewState>();
    qRegisterMetaType<NavigationTarget>();

    connect(m_flowCoordinator, &IAppFlowCoordinator::flowChanged,
            this, &UserUiBinder::handleFlowChanged);
    connect(m_flowCoordinator, &IAppFlowCoordinator::navigationRequested,
            this, &IUserUiBinder::navigationRequested);
    connect(m_userService, &IUserService::operationStatusChanged,
            this, &UserUiBinder::handleOperationStatusChanged);

    rebuildViewStates(m_flowCoordinator->currentFlow());
}

LoginViewState UserUiBinder::currentLoginViewState() const
{
    return m_loginState;
}

ProfileEditViewState UserUiBinder::currentProfileEditViewState() const
{
    return m_profileState;
}

ProfileViewState UserUiBinder::currentProfileViewState() const
{
    return m_profileSummaryState;
}

void UserUiBinder::loginRequested(const QString &phone)
{
    m_loginState.phoneInput = phone;
    m_flowCoordinator->login(phone);
}

void UserUiBinder::usernamePasswordLoginRequested(const QString &username,
                                                  const QString &password)
{
    m_loginState.phoneInput = username;
    m_flowCoordinator->loginByCredentials(username, password);
}

void UserUiBinder::profileSaveRequested(const QString &nickname)
{
    m_profileState.nicknameInput = nickname;
    m_flowCoordinator->saveNickname(nickname);
}

void UserUiBinder::avatarUpdateRequested(const QString &avatarDataUri)
{
    ProfileEditViewState previewState = m_profileState;
    previewState.avatarDataUri = avatarDataUri;
    publishProfileState(previewState);
    m_userService->updateAvatar(avatarDataUri);
}

void UserUiBinder::retryRequested()
{
    m_flowCoordinator->retry();
}

void UserUiBinder::logoutRequested()
{
    m_flowCoordinator->logout();
}

void UserUiBinder::handleFlowChanged(const UserFlowSnapshot &snapshot)
{
    rebuildViewStates(snapshot);
}

void UserUiBinder::handleOperationStatusChanged(const UserOperationStatus &status)
{
    Q_UNUSED(status)
    rebuildViewStates(m_flowCoordinator->currentFlow());
}

SubmitState UserUiBinder::submitStateForError(const ClientError &error)
{
    if (error.resultUnknown) {
        return SubmitState::ResultUnknown;
    }
    if (error.code.startsWith(QStringLiteral("invalid-"))) {
        return SubmitState::ValidationError;
    }
    if (isNetworkErrorCode(error.code)) {
        return SubmitState::NetworkError;
    }
    return SubmitState::ServerError;
}

void UserUiBinder::rebuildViewStates(const UserFlowSnapshot &snapshot)
{
    LoginViewState login = m_loginState;
    login.message.clear();
    login.canRetry = false;

    if (snapshot.state == UserFlowState::SignedOut) {
        // 非 null 的空字符串表示 UI 必须清空输入；null 保留给“不覆盖当前输入”。
        login.phoneInput = QStringLiteral("");
    }

    const UserOperationStatus loginOperation =
        m_userService->operationStatus(UserOperation::Login);
    if (snapshot.state == UserFlowState::LoggingIn
        || loginOperation.state == UserOperationState::Running) {
        login.submitState = SubmitState::Loading;
        login.canSubmit = false;
    } else if (snapshot.state == UserFlowState::Error
               && snapshot.target == NavigationTarget::Login
               && !snapshot.error.code.isEmpty()) {
        login.submitState = submitStateForError(snapshot.error);
        login.message = snapshot.error.displayMessage;
        login.canSubmit = true;
        login.canRetry = snapshot.error.retryable;
    } else if (snapshot.session.authenticated
               && snapshot.target != NavigationTarget::Login) {
        login.submitState = SubmitState::Success;
        login.canSubmit = false;
    } else {
        login.submitState = SubmitState::Idle;
        login.canSubmit = true;
    }

    ProfileEditViewState profile = m_profileState;
    profile.message.clear();
    profile.canRetry = false;

    if (snapshot.state == UserFlowState::SignedOut) {
        profile.phone.clear();
        profile.nicknameInput.clear();
        profile.avatarDataUri.clear();
    } else {
        if (m_userService->operationStatus(UserOperation::UpdateAvatar).state
            != UserOperationState::Running) {
            profile.avatarDataUri = snapshot.session.profile.avatarKey;
        }
        if (!snapshot.session.profile.phone.isEmpty()) {
            profile.phone = snapshot.session.profile.phone;
        }
        if (!snapshot.draftNickname.isEmpty()) {
            profile.nicknameInput = snapshot.draftNickname;
        } else if (!snapshot.session.profile.nickname.isEmpty()) {
            profile.nicknameInput = snapshot.session.profile.nickname;
        }
    }

    const UserOperationStatus nicknameOperation =
        m_userService->operationStatus(UserOperation::UpdateNickname);
    const UserOperationStatus avatarOperation =
        m_userService->operationStatus(UserOperation::UpdateAvatar);
    if (snapshot.state == UserFlowState::RecoveringProfileUpdate
        || nicknameOperation.state == UserOperationState::ResultUnknown
        || avatarOperation.state == UserOperationState::ResultUnknown) {
        profile.submitState = SubmitState::ResultUnknown;
        profile.message = snapshot.error.displayMessage;
        profile.canSubmit = false;
        profile.canRetry = snapshot.error.retryable;
    } else if (nicknameOperation.state == UserOperationState::Running
               || avatarOperation.state == UserOperationState::Running
               || snapshot.state == UserFlowState::InitializingNewUser) {
        profile.submitState = SubmitState::Loading;
        profile.canSubmit = false;
    } else if (!snapshot.error.code.isEmpty()
               && snapshot.target == NavigationTarget::ProfileEdit) {
        profile.submitState = submitStateForError(snapshot.error);
        profile.message = snapshot.error.displayMessage;
        profile.canSubmit = true;
        profile.canRetry = snapshot.error.retryable;
    } else if (snapshot.state == UserFlowState::Ready
               || snapshot.state == UserFlowState::Restricted) {
        profile.submitState = SubmitState::Success;
        profile.canSubmit = true;
    } else {
        profile.submitState = SubmitState::Idle;
        profile.canSubmit = snapshot.state == UserFlowState::ProfileRequired;
    }

    ProfileViewState profileSummary;
    if (snapshot.session.authenticated) {
        profileSummary.nickname = snapshot.session.profile.nickname;
        profileSummary.avatarDataUri = snapshot.session.profile.avatarKey;
        profileSummary.maskedPhone = maskedPhone(snapshot.session.profile.phone);
        if (snapshot.session.profile.balanceCents) {
            profileSummary.balanceText = QStringLiteral("¥%1")
                .arg(*snapshot.session.profile.balanceCents / 100.0, 0, 'f', 2);
        }
        switch (snapshot.session.accountStatus) {
        case AccountStatus::Normal:
            profileSummary.accountState = AccountDisplayState::Normal;
            break;
        case AccountStatus::Frozen:
            profileSummary.accountState = AccountDisplayState::Frozen;
            profileSummary.accountMessage =
                QStringLiteral("当前账号已冻结，部分操作暂不可用。");
            break;
        case AccountStatus::Unknown:
            profileSummary.accountState = AccountDisplayState::Unknown;
            profileSummary.accountMessage =
                QStringLiteral("账号状态尚未确认，请稍后重试。");
            break;
        }
    }

    publishLoginState(login);
    publishProfileState(profile);
    publishProfileSummaryState(profileSummary);
}

void UserUiBinder::publishLoginState(const LoginViewState &state)
{
    if (loginStatesEqual(m_loginState, state)) {
        return;
    }
    m_loginState = state;
    emit loginViewStateChanged(m_loginState);
}

void UserUiBinder::publishProfileState(const ProfileEditViewState &state)
{
    if (profileStatesEqual(m_profileState, state)) {
        return;
    }
    m_profileState = state;
    emit profileEditViewStateChanged(m_profileState);
}

void UserUiBinder::publishProfileSummaryState(const ProfileViewState &state)
{
    if (profileSummaryStatesEqual(m_profileSummaryState, state)) {
        return;
    }
    m_profileSummaryState = state;
    emit profileViewStateChanged(m_profileSummaryState);
}
