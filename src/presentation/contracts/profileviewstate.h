#pragma once

#include <QString>

enum class AccountDisplayState {
    Normal,
    Frozen,
    Unknown
};

struct ProfileViewState {
    QString nickname;
    QString maskedPhone;
    QString balanceText;
    QString avatarDataUri;
    AccountDisplayState accountState = AccountDisplayState::Unknown;
    QString accountMessage;
};
