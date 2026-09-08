#pragma once

#include <QMetaType>
#include <QString>

#include <optional>

enum class AccountStatus
{
    Normal,
    Frozen,
    Unknown
};

struct UserProfile
{
    QString userId;
    QString phone;
    QString nickname;
    QString avatarKey;
    /** 服务端 user.balanceCents；缺失时保持 unknown，不能伪造为 0。 */
    std::optional<qint64> balanceCents;
};

struct UserSession
{
    UserProfile profile;
    AccountStatus accountStatus = AccountStatus::Unknown;
    bool authenticated = false;
};

struct LoginResult
{
    QString requestId;
    UserSession session;
    bool isNewUser = false;
    bool profileCompleted = false;
};

struct UserProfileResult
{
    QString requestId;
    QString operationId;
    UserProfile profile;
    AccountStatus accountStatus = AccountStatus::Unknown;
};

/* 用户服务的操作状态（第一步规格 §一/§二.1）：
 * 供 UI Binder 生成 Loading/Idle/ResultUnknown 展示状态；
 * 不依赖 Widget、协议编号或 JSON */
enum class UserOperation
{
    Login,
    RefreshProfile,
    UpdateNickname,
    Logout
};

enum class UserOperationState
{
    Idle,
    Running,
    ResultUnknown
};

struct UserOperationStatus
{
    UserOperation operation = UserOperation::Login;
    UserOperationState state = UserOperationState::Idle;
    QString requestId;
    QString operationId;
};

Q_DECLARE_METATYPE(AccountStatus)
Q_DECLARE_METATYPE(UserProfile)
Q_DECLARE_METATYPE(UserSession)
Q_DECLARE_METATYPE(LoginResult)
Q_DECLARE_METATYPE(UserProfileResult)
Q_DECLARE_METATYPE(UserOperation)
Q_DECLARE_METATYPE(UserOperationState)
Q_DECLARE_METATYPE(UserOperationStatus)
