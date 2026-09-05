#pragma once

#include <QString>

#include "submitstate.h"

struct ProfileEditViewState {
    SubmitState submitState = SubmitState::Idle;
    QString phone;
    QString nicknameInput;
    QString message;
    bool canSubmit = true;
};
