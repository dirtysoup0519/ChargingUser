#pragma once

/* Binder 使用的页面展示合同汇总头。具体结构由 presentation/contracts
 * 分文件维护，避免 app 层复制 UI 合同定义。
 */

#include "loginviewstate.h"
#include "profileeditviewstate.h"
#include "profileviewstate.h"

#include <QMetaType>

Q_DECLARE_METATYPE(SubmitState)
Q_DECLARE_METATYPE(LoginViewState)
Q_DECLARE_METATYPE(ProfileEditViewState)
Q_DECLARE_METATYPE(AccountDisplayState)
Q_DECLARE_METATYPE(ProfileViewState)
