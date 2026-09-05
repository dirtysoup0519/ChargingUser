#pragma once

/* 用户流程协调层的领域类型（M4 无 UI 流程部分）：
 *  - UserFlowState      流程状态机的状态（登录/初始化/资料完善/就绪/受限/恢复/错误）
 *  - NavigationTarget   建议的页面去向（仅"建议"，最终跳转由 UI Binder 决定）
 *  - UserFlowSnapshot   一次快照 = 状态 + 去向 + 会话 + 最近错误 + 昵称草稿；
 *                       UI 只消费快照渲染，不直接访问 UserService
 * 本文件与窗口/页面控件、协议载荷解析、Socket 传输均无任何依赖。
 */

#include "common/clienterror.h"
#include "modules/user/usertypes.h"

#include <QMetaType>
#include <QString>

enum class UserFlowState
{
    SignedOut,               // 未登录（含退出后）
    LoggingIn,               // 登录请求在途
    InitializingNewUser,     // 新用户：默认昵称提交在途
    RefreshingProfile,       // 老用户：登录后的资料刷新在途
    ProfileRequired,         // 资料完善场景（新用户/默认昵称失败后的落点）
    Ready,                   // 正常用户就绪
    Restricted,              // 受限用户（Frozen/Unknown）
    RecoveringProfileUpdate, // 昵称修改结果未知：等待资料刷新确认
    Error                    // 终态错误（登录/刷新失败、恢复确认不一致）
};

enum class NavigationTarget
{
    Login,
    ProfileEdit,
    Home,
    RestrictedHome
};

struct UserFlowSnapshot
{
    UserFlowState state = UserFlowState::SignedOut;
    NavigationTarget target = NavigationTarget::Login;
    UserSession session;
    ClientError error;
    QString draftNickname;   // 昵称草稿：保存失败/恢复确认不一致时保留，供用户续编
};

Q_DECLARE_METATYPE(UserFlowState)
Q_DECLARE_METATYPE(NavigationTarget)
Q_DECLARE_METATYPE(UserFlowSnapshot)
