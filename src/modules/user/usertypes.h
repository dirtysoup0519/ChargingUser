#pragma once

#include <QMetaType>
#include <QString>

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

Q_DECLARE_METATYPE(AccountStatus)
Q_DECLARE_METATYPE(UserProfile)
Q_DECLARE_METATYPE(UserSession)
Q_DECLARE_METATYPE(LoginResult)
Q_DECLARE_METATYPE(UserProfileResult)
