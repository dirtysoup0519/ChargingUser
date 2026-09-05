#pragma once

/* 页面展示合同：只描述 UI 应该展示的状态，不包含 Widget、协议或业务对象。
 * 页面负责 render()，UserUiBinder 负责生成这些快照。
 */

#include <QMetaType>
#include <QString>

enum class SubmitState
{
    Idle,
    Loading,
    Success,
    ValidationError,
    NetworkError,
    ServerError,
    ResultUnknown
};

struct LoginViewState
{
    SubmitState submitState = SubmitState::Idle;
    QString phoneInput;
    QString message;
    bool canSubmit = true;
    bool canRetry = false;
};

struct ProfileEditViewState
{
    SubmitState submitState = SubmitState::Idle;
    QString phone;
    QString nicknameInput;
    QString message;
    bool canSubmit = true;
    bool canRetry = false;
};

Q_DECLARE_METATYPE(SubmitState)
Q_DECLARE_METATYPE(LoginViewState)
Q_DECLARE_METATYPE(ProfileEditViewState)
