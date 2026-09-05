#pragma once

#include <QString>

#include "submitstate.h"

struct LoginViewState {
    SubmitState submitState = SubmitState::Idle;
    QString phoneInput;
    QString message;
    bool canSubmit = true;
    bool canRetry = false;
};
