#pragma once

#include <QString>

#include "submitstate.h"

struct ProfileEditViewState {
    SubmitState submitState = SubmitState::Idle;
    QString phone;
    QString username;
    QString nicknameInput;
    QString avatarDataUri;
    QString message;
    bool canSubmit = true;
    bool canRetry = false;
};
