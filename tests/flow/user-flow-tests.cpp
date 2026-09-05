/* 用户流程协调器测试（M4 无 UI 流程）
 * 架构：MockUserNetworkApi + UserService + AppFlowCoordinator
 * 规格要求：不创建 QApplication/窗口，不依赖真实服务器（用 QTEST_GUILESS_MAIN）
 * 覆盖：登录 → 新老用户分流 → 资料处理 → 权限分流 → 退出 的业务闭环
 */
#include "flow/appflowcoordinator.h"
#include "modules/user/mockusernetworkapi.h"
#include "modules/user/userservice.h"

#include <QSignalSpy>
#include <QtTest>

namespace
{

/* 构造与 userId 匹配的资料应答（服务层会做串号防御，userId 必须一致） */
UserProfileResult makeProfileResult(const QString &userId,
                                    const QString &nickname,
                                    AccountStatus status)
{
    UserProfileResult result;
    result.profile.userId = userId;
    result.profile.phone = userId;
    result.profile.nickname = nickname;
    result.accountStatus = status;
    return result;
}

/* 流程夹具：Service + 协调器 + 三路信号监听 */
struct FlowFixture
{
    explicit FlowFixture()
    {
        qRegisterMetaType<UserFlowSnapshot>();
        qRegisterMetaType<NavigationTarget>();
    }

    MockUserNetworkApi network;
    UserService service{&network};
    AppFlowCoordinator coordinator{&service};
    QSignalSpy flows{&coordinator, &IAppFlowCoordinator::flowChanged};
    QSignalSpy navigations{&coordinator, &IAppFlowCoordinator::navigationRequested};
    QSignalSpy failures{&service, &IUserService::operationFailed};
};

/* 老用户登录直到资料刷新完成（Normal 账号 → Ready + Home） */
void loginAsOldUser(FlowFixture &f, const QString &phone = QStringLiteral("13800138000"))
{
    LoginResult login;
    login.session.profile.userId = phone;
    login.session.profile.phone = phone;
    login.session.accountStatus = AccountStatus::Normal;
    f.network.setLoginResult(login);
    f.network.setUserProfileResult(makeProfileResult(phone, QStringLiteral("老王"),
                                                     AccountStatus::Normal));
    f.coordinator.login(phone);
    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::Ready);
}

} // namespace

class UserFlowTests final : public QObject
{
    Q_OBJECT

private slots:
    void oldUserLoginNavigatesHome();
    void frozenUserNavigatesRestrictedHome();
    void newUserGoesProfileEditWithDefaultNickname();
    void newUserDefaultNicknameFailureStillProfileEdit();
    void profileRequiredSaveSuccessNavigatesHome();
    void profileRequiredFrozenSaveGoesRestrictedHome();
    void saveResultUnknownConfirmedByRefresh();
    void saveResultUnknownMismatchGoesError();
    void loginFailureRetrySucceeds();
    void refreshFailureRetrySucceeds();
    void logoutGoesLoginImmediatelyAndLateResponsesIgnored();
    void logoutThenImmediateLoginIsQueuedAndSucceeds();
    void reloginClearsPreviousAccountContext();
    void nicknameUnknownRecoveryRefreshFailureRetriesRefresh();
    void noAutoResubmitAfterMismatch();
    void frozenUserSaveNicknameStaysRestricted();
    void saveNicknameBlockedAfterLoginOrRefreshFailure();
    void defaultNicknameUsesLoginResultPhoneNotStaleState();
};

/* 老用户登录 → 刷新资料 → Home */
void UserFlowTests::oldUserLoginNavigatesHome()
{
    FlowFixture f;
    loginAsOldUser(f);

    const UserFlowSnapshot flow = f.coordinator.currentFlow();
    QCOMPARE(flow.state, UserFlowState::Ready);
    QCOMPARE(flow.target, NavigationTarget::Home);
    QCOMPARE(flow.session.profile.nickname, QStringLiteral("老王"));
    QVERIFY(flow.session.authenticated);

    // 导航建议恰好出现一次 Home（登录开始时不发导航）
    QCOMPARE(f.navigations.count(), 1);
    QCOMPARE(f.navigations.takeFirst().at(0).value<NavigationTarget>(),
             NavigationTarget::Home);
}

/* Frozen/Unknown 用户 → RestrictedHome（权限分流） */
void UserFlowTests::frozenUserNavigatesRestrictedHome()
{
    FlowFixture f;
    LoginResult login;
    login.session.profile.userId = QStringLiteral("13800138000");
    login.session.profile.phone = QStringLiteral("13800138000");
    login.session.accountStatus = AccountStatus::Frozen;
    f.network.setLoginResult(login);
    f.network.setUserProfileResult(makeProfileResult(QStringLiteral("13800138000"),
                                                     QStringLiteral("老王"),
                                                     AccountStatus::Frozen));
    f.coordinator.login(QStringLiteral("13800138000"));

    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::Restricted);
    QCOMPARE(f.coordinator.currentFlow().target, NavigationTarget::RestrictedHome);

    // Unknown 账号同样受限
    FlowFixture f2;
    login.session.accountStatus = AccountStatus::Unknown;
    f2.network.setLoginResult(login);
    f2.network.setUserProfileResult(makeProfileResult(QStringLiteral("13800138000"),
                                                      QStringLiteral("老王"),
                                                      AccountStatus::Unknown));
    f2.coordinator.login(QStringLiteral("13800138000"));
    QTRY_COMPARE(f2.coordinator.currentFlow().state, UserFlowState::Restricted);
    QCOMPARE(f2.coordinator.currentFlow().target, NavigationTarget::RestrictedHome);
}

/* 新用户 → 默认昵称请求 → ProfileEdit */
void UserFlowTests::newUserGoesProfileEditWithDefaultNickname()
{
    FlowFixture f;
    LoginResult login;
    login.session.profile.userId = QStringLiteral("13800138000");
    login.session.profile.phone = QStringLiteral("13800138000");
    login.session.accountStatus = AccountStatus::Normal;
    login.isNewUser = true;
    f.network.setLoginResult(login);
    f.coordinator.login(QStringLiteral("13800138000"));

    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::ProfileRequired);
    QCOMPARE(f.coordinator.currentFlow().target, NavigationTarget::ProfileEdit);
    // 默认昵称 = "用户" + 手机号后四位（合同 §11.2 R8）
    QCOMPARE(f.network.lastNickname(), QStringLiteral("用户8000"));
    QCOMPARE(f.coordinator.currentFlow().draftNickname, QStringLiteral("用户8000"));
    QVERIFY(f.coordinator.currentFlow().session.authenticated);
}

/* 新用户默认昵称提交失败 → 仍进 ProfileEdit 且保留默认昵称草稿、保留会话 */
void UserFlowTests::newUserDefaultNicknameFailureStillProfileEdit()
{
    FlowFixture f;
    LoginResult login;
    login.session.profile.userId = QStringLiteral("13800138000");
    login.session.profile.phone = QStringLiteral("13800138000");
    login.session.accountStatus = AccountStatus::Normal;
    login.isNewUser = true;
    f.network.setLoginResult(login);
    MockUserNetworkApi::Behavior failure;
    failure.outcome = MockUserNetworkApi::Outcome::Failure;
    f.network.setNicknameBehavior(failure);
    f.coordinator.login(QStringLiteral("13800138000"));

    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::ProfileRequired);
    QCOMPARE(f.coordinator.currentFlow().target, NavigationTarget::ProfileEdit);
    QCOMPARE(f.coordinator.currentFlow().draftNickname, QStringLiteral("用户8000"));
    QVERIFY(f.coordinator.currentFlow().session.authenticated);
}

/* 资料完善场景保存成功（Normal）→ Home */
void UserFlowTests::profileRequiredSaveSuccessNavigatesHome()
{
    FlowFixture f;
    LoginResult login;
    login.session.profile.userId = QStringLiteral("13800138000");
    login.session.profile.phone = QStringLiteral("13800138000");
    login.session.accountStatus = AccountStatus::Normal;
    login.isNewUser = true;
    f.network.setLoginResult(login);
    f.coordinator.login(QStringLiteral("13800138000"));
    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::ProfileRequired);

    f.coordinator.saveNickname(QStringLiteral("老王"));
    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::Ready);
    QCOMPARE(f.coordinator.currentFlow().target, NavigationTarget::Home);
    QVERIFY(f.coordinator.currentFlow().draftNickname.isEmpty());
}

/* 资料完善场景保存成功但账号受限（Frozen）→ RestrictedHome */
void UserFlowTests::profileRequiredFrozenSaveGoesRestrictedHome()
{
    FlowFixture f;
    LoginResult login;
    login.session.profile.userId = QStringLiteral("13800138000");
    login.session.profile.phone = QStringLiteral("13800138000");
    login.session.accountStatus = AccountStatus::Frozen;
    login.isNewUser = true;
    f.network.setLoginResult(login);
    f.coordinator.login(QStringLiteral("13800138000"));
    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::ProfileRequired);

    f.coordinator.saveNickname(QStringLiteral("老王"));
    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::Restricted);
    QCOMPARE(f.coordinator.currentFlow().target, NavigationTarget::RestrictedHome);
}

/* 资料保存结果未知 → 刷新确认昵称相同 → 视为保存成功（不重发修改） */
void UserFlowTests::saveResultUnknownConfirmedByRefresh()
{
    FlowFixture f;
    loginAsOldUser(f);   // 老用户就绪（内含登录前查询应答设置，避免串号防御拦截）

    MockUserNetworkApi::Behavior unknown;
    unknown.outcome = MockUserNetworkApi::Outcome::ResultUnknown;
    f.network.setNicknameBehavior(unknown);
    // 给确认刷新加延迟：mock 瞬时应答会让 RecoveringProfileUpdate 中间态来不及观测
    MockUserNetworkApi::Behavior slowQuery;
    slowQuery.delayMs = 30;
    f.network.setQueryBehavior(slowQuery);
    f.coordinator.saveNickname(QStringLiteral("老王"));
    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::RecoveringProfileUpdate);

    // 协调器应已自动发起资料刷新确认（登录刷新 1 次 + 恢复确认 1 次）
    QTRY_COMPARE(f.network.queryRequestCount(), 2);
    QCOMPARE(f.network.nicknameRequestCount(), 1);   // 未自动重发修改

    // 刷新确认服务端昵称与草稿一致 → 保存成功；非资料完善场景不强制导航
    f.network.setUserProfileResult(makeProfileResult(QStringLiteral("13800138000"),
                                                     QStringLiteral("老王"),
                                                     AccountStatus::Normal));
    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::Ready);
    QVERIFY(f.coordinator.currentFlow().draftNickname.isEmpty());
    QCOMPARE(f.network.nicknameRequestCount(), 1);
}

/* 资料保存结果未知 → 刷新昵称不一致 → Error，保留草稿，无自动重提 */
void UserFlowTests::saveResultUnknownMismatchGoesError()
{
    FlowFixture f;
    loginAsOldUser(f);   // 老用户就绪（内含查询应答设置）

    MockUserNetworkApi::Behavior unknown;
    unknown.outcome = MockUserNetworkApi::Outcome::ResultUnknown;
    f.network.setNicknameBehavior(unknown);
    // 给确认刷新加延迟：mock 瞬时应答会让 RecoveringProfileUpdate 中间态来不及观测
    MockUserNetworkApi::Behavior slowQuery;
    slowQuery.delayMs = 30;
    f.network.setQueryBehavior(slowQuery);
    // 应答载荷在刷新派发时刻快照：必须先设置不一致的服务端应答再触发刷新
    f.network.setUserProfileResult(makeProfileResult(QStringLiteral("13800138000"),
                                                     QStringLiteral("别人改的"),
                                                     AccountStatus::Normal));
    f.coordinator.saveNickname(QStringLiteral("老王"));
    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::RecoveringProfileUpdate);

    // 服务端昵称与预期不符 → Error，保留草稿
    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::Error);
    QCOMPARE(f.coordinator.currentFlow().draftNickname, QStringLiteral("老王"));
    QCOMPARE(f.network.nicknameRequestCount(), 1);   // 无自动重提

    // Error 后不允许 retry 重发昵称修改
    f.coordinator.retry();
    QCOMPARE(f.network.nicknameRequestCount(), 1);
}

/* 登录失败 retry 后成功 */
void UserFlowTests::loginFailureRetrySucceeds()
{
    FlowFixture f;
    LoginResult login;
    login.session.profile.userId = QStringLiteral("13800138000");
    login.session.profile.phone = QStringLiteral("13800138000");
    login.session.accountStatus = AccountStatus::Normal;
    f.network.setLoginResult(login);
    MockUserNetworkApi::Behavior failure;
    failure.outcome = MockUserNetworkApi::Outcome::Failure;
    f.network.setLoginBehavior(failure);

    f.coordinator.login(QStringLiteral("13800138000"));
    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::Error);
    QCOMPARE(f.coordinator.currentFlow().target, NavigationTarget::Login);
    QVERIFY(!f.coordinator.currentFlow().error.code.isEmpty());

    // 登录失败 → Error（可 retry）；retry 前需设置登录刷新应答
    MockUserNetworkApi::Behavior ok;
    f.network.setLoginBehavior(ok);
    f.network.setUserProfileResult(makeProfileResult(QStringLiteral("13800138000"),
                                                     QStringLiteral("老王"),
                                                     AccountStatus::Normal));
    f.coordinator.retry();
    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::Ready);
    QCOMPARE(f.coordinator.currentFlow().target, NavigationTarget::Home);
    QCOMPARE(f.network.loginRequestCount(), 2);
}

/* 老用户资料刷新失败 retry 后成功（规格 §7：retry 允许资料刷新失败） */
void UserFlowTests::refreshFailureRetrySucceeds()
{
    FlowFixture f;
    LoginResult login;
    login.session.profile.userId = QStringLiteral("13800138000");
    login.session.profile.phone = QStringLiteral("13800138000");
    login.session.accountStatus = AccountStatus::Normal;
    f.network.setLoginResult(login);
    MockUserNetworkApi::Behavior queryFailure;
    queryFailure.outcome = MockUserNetworkApi::Outcome::Failure;
    f.network.setQueryBehavior(queryFailure);

    f.coordinator.login(QStringLiteral("13800138000"));
    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::Error);

    MockUserNetworkApi::Behavior queryOk;
    f.network.setQueryBehavior(queryOk);
    f.network.setUserProfileResult(makeProfileResult(QStringLiteral("13800138000"),
                                                     QStringLiteral("老王"),
                                                     AccountStatus::Normal));
    f.coordinator.retry();
    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::Ready);
    QCOMPARE(f.coordinator.currentFlow().target, NavigationTarget::Home);
    QCOMPARE(f.network.queryRequestCount(), 2);
}

/* 退出立即进入 Login；迟到响应不得改变流程 */
void UserFlowTests::logoutGoesLoginImmediatelyAndLateResponsesIgnored()
{
    FlowFixture f;
    LoginResult login;
    login.session.profile.userId = QStringLiteral("13800138000");
    login.session.profile.phone = QStringLiteral("13800138000");
    login.session.accountStatus = AccountStatus::Normal;
    f.network.setLoginResult(login);
    MockUserNetworkApi::Behavior slowQuery;
    slowQuery.delayMs = 60;
    f.network.setQueryBehavior(slowQuery);

    f.coordinator.login(QStringLiteral("13800138000"));
    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::RefreshingProfile);

    f.coordinator.logout();
    // 立即（无需等待应答）回 SignedOut + 导航 Login
    QCOMPARE(f.coordinator.currentFlow().state, UserFlowState::SignedOut);
    QCOMPARE(f.coordinator.currentFlow().target, NavigationTarget::Login);
    QCOMPARE(f.navigations.count(), 1);
    QCOMPARE(f.navigations.takeFirst().at(0).value<NavigationTarget>(),
             NavigationTarget::Login);

    // 迟到的资料刷新成功不得恢复会话或改变流程
    QTest::qWait(100);
    QCOMPARE(f.coordinator.currentFlow().state, UserFlowState::SignedOut);
    QVERIFY(!f.coordinator.currentFlow().session.authenticated);
}

/* 换号登录不保留旧账号 session、错误、昵称草稿 */
void UserFlowTests::reloginClearsPreviousAccountContext()
{
    FlowFixture f;
    // 账号 A：新用户，默认昵称失败 → ProfileRequired + 草稿"用户8000"
    LoginResult loginA;
    loginA.session.profile.userId = QStringLiteral("13800138000");
    loginA.session.profile.phone = QStringLiteral("13800138000");
    loginA.session.accountStatus = AccountStatus::Normal;
    loginA.isNewUser = true;
    f.network.setLoginResult(loginA);
    MockUserNetworkApi::Behavior nickFailure;
    nickFailure.outcome = MockUserNetworkApi::Outcome::Failure;
    f.network.setNicknameBehavior(nickFailure);
    f.coordinator.login(QStringLiteral("13800138000"));
    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::ProfileRequired);
    QCOMPARE(f.coordinator.currentFlow().draftNickname, QStringLiteral("用户8000"));

    f.coordinator.logout();
    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::SignedOut);
    // 退出通知是异步的：等 Logout 操作回 Idle 再换号，避免服务层防重拦截登录
    QTRY_COMPARE(f.service.operationStatus(UserOperation::Logout).state,
                 UserOperationState::Idle);

    // 账号 B：老用户登录 → 上下文必须全部清空
    LoginResult loginB;
    loginB.session.profile.userId = QStringLiteral("13900139000");
    loginB.session.profile.phone = QStringLiteral("13900139000");
    loginB.session.accountStatus = AccountStatus::Normal;
    f.network.setLoginResult(loginB);
    // B 的登录刷新需要匹配的查询应答（userId 一致，否则触发串号防御）
    f.network.setUserProfileResult(makeProfileResult(QStringLiteral("13900139000"),
                                                     QStringLiteral("用户13800138000"),
                                                     AccountStatus::Normal));
    f.network.setNicknameBehavior(MockUserNetworkApi::Behavior{});
    f.coordinator.login(QStringLiteral("13900139000"));

    // 进入 LoggingIn 后立即检查：无 A 的会话、错误、草稿
    QVERIFY(!f.coordinator.currentFlow().session.authenticated);
    QVERIFY(f.coordinator.currentFlow().error.code.isEmpty());
    QVERIFY(f.coordinator.currentFlow().draftNickname.isEmpty());
    QCOMPARE(f.coordinator.currentFlow().state, UserFlowState::LoggingIn);

    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::Ready);
    QCOMPARE(f.coordinator.currentFlow().session.profile.userId,
             QStringLiteral("13900139000"));
}

/* 结果未知恢复期间刷新失败 → retry() 重新刷新确认（不是重发修改） */
void UserFlowTests::nicknameUnknownRecoveryRefreshFailureRetriesRefresh()
{
    FlowFixture f;
    loginAsOldUser(f);   // 老用户就绪（内含查询应答设置，登录刷新耗用 1 次查询）

    MockUserNetworkApi::Behavior unknown;
    unknown.outcome = MockUserNetworkApi::Outcome::ResultUnknown;
    f.network.setNicknameBehavior(unknown);
    // 恢复刷新失败需在派发前生效（mock 在派发时刻快照行为与载荷）
    MockUserNetworkApi::Behavior queryFailure;
    queryFailure.outcome = MockUserNetworkApi::Outcome::Failure;
    f.network.setQueryBehavior(queryFailure);
    f.coordinator.saveNickname(QStringLiteral("老王"));
    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::RecoveringProfileUpdate);
    QTRY_COMPARE(f.failures.count(), 2);   // 昵称结果未知 1 次 + 恢复刷新失败 1 次

    f.coordinator.retry();
    QCOMPARE(f.coordinator.currentFlow().state, UserFlowState::RecoveringProfileUpdate);
    QCOMPARE(f.network.nicknameRequestCount(), 1);
    // 等第一次重试的刷新失败落地：在途请求未释放前，服务层会防重拦截第二次派发
    QTRY_COMPARE(f.failures.count(), 3);

    // 重试刷新成功且昵称一致 → 保存成功（成功应答同样需在派发前设置）
    MockUserNetworkApi::Behavior queryOk;
    f.network.setQueryBehavior(queryOk);
    f.network.setUserProfileResult(makeProfileResult(QStringLiteral("13800138000"),
                                                     QStringLiteral("老王"),
                                                     AccountStatus::Normal));
    f.coordinator.retry();
    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::Ready);
    // 查询总数：登录 1 + 恢复刷新 2 + 第一次重试 3 + 第二次重试 4
    QCOMPARE(f.network.queryRequestCount(), 4);
}

/* 结果未知恢复成功后无自动重提的补充断言：确认一致即落定 Ready */
void UserFlowTests::noAutoResubmitAfterMismatch()
{
    // 与 saveResultUnknownMismatchGoesError 互补：验证确认一致路径的请求次数
    FlowFixture f;
    loginAsOldUser(f);   // 老用户就绪（内含查询应答设置，登录刷新耗用 1 次查询）

    MockUserNetworkApi::Behavior unknown;
    unknown.outcome = MockUserNetworkApi::Outcome::ResultUnknown;
    f.network.setNicknameBehavior(unknown);
    // 给确认刷新加延迟：mock 瞬时应答会让 RecoveringProfileUpdate 中间态来不及观测
    MockUserNetworkApi::Behavior slowQuery;
    slowQuery.delayMs = 30;
    f.network.setQueryBehavior(slowQuery);
    f.coordinator.saveNickname(QStringLiteral("老王"));
    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::RecoveringProfileUpdate);

    f.network.setUserProfileResult(makeProfileResult(QStringLiteral("13800138000"),
                                                     QStringLiteral("老王"),
                                                     AccountStatus::Normal));
    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::Ready);

    // 全程：1 次昵称提交 + 2 次查询（登录 1 次 + 确认 1 次），绝无第二次昵称提交
    QCOMPARE(f.network.nicknameRequestCount(), 1);
    QCOMPARE(f.network.queryRequestCount(), 2);
}

/* 退出后立即登录：登录被排队，Logout 终态后自动执行（评审 P1-2） */
void UserFlowTests::logoutThenImmediateLoginIsQueuedAndSucceeds()
{
    FlowFixture f;
    loginAsOldUser(f);

    // 账号 B 的登录与刷新应答需在派发前设置（mock 派发时刻快照）
    LoginResult loginB;
    loginB.session.profile.userId = QStringLiteral("13900139000");
    loginB.session.profile.phone = QStringLiteral("13900139000");
    loginB.session.accountStatus = AccountStatus::Normal;
    f.network.setLoginResult(loginB);
    f.network.setUserProfileResult(makeProfileResult(QStringLiteral("13900139000"),
                                                     QStringLiteral("用户13900139000"),
                                                     AccountStatus::Normal));

    f.coordinator.logout();
    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::SignedOut);

    // 不等退出完成立即登录：应进入 LoggingIn 且请求被排队（不调用服务层）
    f.coordinator.login(QStringLiteral("13900139000"));
    QCOMPARE(f.coordinator.currentFlow().state, UserFlowState::LoggingIn);
    QCOMPARE(f.network.loginRequestCount(), 1);   // 仍是账号 A 那次，B 尚未发出

    // Logout 终态后排队登录自动执行并走完整流程 → Ready
    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::Ready);
    QCOMPARE(f.network.loginRequestCount(), 2);
    QCOMPARE(f.coordinator.currentFlow().session.profile.userId,
             QStringLiteral("13900139000"));
    QCOMPARE(f.coordinator.currentFlow().target, NavigationTarget::Home);
}

/* Frozen 用户在"我的"页面改昵称成功：保持 Restricted，不错变 Ready（评审 P1-3） */
void UserFlowTests::frozenUserSaveNicknameStaysRestricted()
{
    FlowFixture f;
    LoginResult login;
    login.session.profile.userId = QStringLiteral("13800138000");
    login.session.profile.phone = QStringLiteral("13800138000");
    login.session.accountStatus = AccountStatus::Frozen;
    f.network.setLoginResult(login);
    f.network.setUserProfileResult(makeProfileResult(QStringLiteral("13800138000"),
                                                     QStringLiteral("13800138000"),
                                                     AccountStatus::Frozen));
    f.coordinator.login(QStringLiteral("13800138000"));
    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::Restricted);
    QCOMPARE(f.coordinator.currentFlow().target, NavigationTarget::RestrictedHome);
    QSignalSpy navigations{&f.coordinator, &IAppFlowCoordinator::navigationRequested};
    const int navigationsBefore = navigations.count();

    f.coordinator.saveNickname(QStringLiteral("老王"));
    QTRY_COMPARE(f.coordinator.currentFlow().session.profile.nickname,
                 QStringLiteral("老王"));

    // 保存成功后保持 Restricted，不强制导航
    QCOMPARE(f.coordinator.currentFlow().state, UserFlowState::Restricted);
    QCOMPARE(f.coordinator.currentFlow().target, NavigationTarget::RestrictedHome);
    QVERIFY(f.coordinator.currentFlow().draftNickname.isEmpty());
    QCOMPARE(navigations.count(), navigationsBefore);
}

/* 验收重点 3：登录失败 / 资料刷新失败产生的 Error 态，saveNickname 不发送请求，
 * 且不覆盖草稿——这两类失败只能经 retry()/重新登录走出 Error */
void UserFlowTests::saveNicknameBlockedAfterLoginOrRefreshFailure()
{
    FlowFixture f;
    MockUserNetworkApi::Behavior loginFail;
    loginFail.outcome = MockUserNetworkApi::Outcome::Failure;
    f.network.setLoginBehavior(loginFail);
    f.coordinator.login(QStringLiteral("13800138000"));
    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::Error);

    // 登录失败（未认证）：不发送请求，不覆盖草稿
    f.coordinator.saveNickname(QStringLiteral("草稿A"));
    QCOMPARE(f.network.nicknameRequestCount(), 0);
    QVERIFY(f.coordinator.currentFlow().draftNickname.isEmpty());

    // retry 登录成功 → Ready（登录应答需含匹配 userId，否则服务层串号防御拦截刷新合并）
    LoginResult retryLogin;
    retryLogin.session.profile.userId = QStringLiteral("13800138000");
    retryLogin.session.profile.phone = QStringLiteral("13800138000");
    retryLogin.session.accountStatus = AccountStatus::Normal;
    f.network.setLoginResult(retryLogin);
    MockUserNetworkApi::Behavior ok;
    f.network.setLoginBehavior(ok);
    f.network.setUserProfileResult(makeProfileResult(QStringLiteral("13800138000"),
                                                     QStringLiteral("13800138000"),
                                                     AccountStatus::Normal));
    f.coordinator.retry();
    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::Ready);

    // 资料刷新失败 → Error：Ready 后 retry() 是 no-op，重新登录触发查询失败。
    MockUserNetworkApi::Behavior queryFail;
    queryFail.outcome = MockUserNetworkApi::Outcome::Failure;
    f.network.setQueryBehavior(queryFail);
    f.coordinator.login(QStringLiteral("13800138000"));
    QTRY_COMPARE(f.coordinator.currentFlow().state, UserFlowState::Error);
    f.coordinator.saveNickname(QStringLiteral("草稿B"));
    QCOMPARE(f.network.nicknameRequestCount(), 0);
    QVERIFY(f.coordinator.currentFlow().draftNickname.isEmpty());
}

/* 双重登录竞态下，默认昵称必须取自登录应答的 phone，而非 m_phone。 */
void UserFlowTests::defaultNicknameUsesLoginResultPhoneNotStaleState()
{
    FlowFixture f;
    LoginResult loginA;
    loginA.session.profile.userId = QStringLiteral("U13800138000");
    loginA.session.profile.phone = QStringLiteral("13800138000");
    loginA.session.accountStatus = AccountStatus::Normal;
    loginA.isNewUser = true;
    f.network.setLoginResult(loginA);
    MockUserNetworkApi::Behavior slowLogin;
    slowLogin.delayMs = 30;
    f.network.setLoginBehavior(slowLogin);

    f.coordinator.login(QStringLiteral("13800138000"));
    f.coordinator.login(QStringLiteral("13900139000"));

    f.network.setUserProfileResult(makeProfileResult(QStringLiteral("U13800138000"),
                                                     QStringLiteral("用户8000"),
                                                     AccountStatus::Normal));
    QTRY_COMPARE(f.network.lastNickname(), QStringLiteral("用户8000"));
    QCOMPARE(f.coordinator.currentFlow().state, UserFlowState::ProfileRequired);
    QCOMPARE(f.coordinator.currentFlow().draftNickname, QStringLiteral("用户8000"));
    QCOMPARE(f.network.loginRequestCount(), 1);
}

QTEST_GUILESS_MAIN(UserFlowTests)

#include "user-flow-tests.moc"
