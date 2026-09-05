#pragma once

/* 应用流程协调器接口（M4 用户流程部分）：
 *  - 输入：UI 发来的语义意图（login/saveNickname/retry/logout 四个槽）
 *  - 输出：flowChanged 快照流 + navigationRequested 导航建议
 *  - 依赖：只依赖 IUserService 及既有业务类型，不接触 UI/协议/Socket。
 * UI Binder 只需：connect 意图 → 槽；flowChanged/navigationRequested → 页面渲染。
 */

#include "userflowtypes.h"

#include <QObject>
#include <QString>

class IAppFlowCoordinator : public QObject
{
    Q_OBJECT

public:
    explicit IAppFlowCoordinator(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~IAppFlowCoordinator() override = default;

    /* 当前流程快照：UI 可随时拉取，无需等待 flowChanged */
    virtual UserFlowSnapshot currentFlow() const = 0;

public slots:
    /* 发起登录：状态进入 LoggingIn；同时开始新的流程生命周期（清空上一账号痕迹） */
    virtual void login(const QString &phone) = 0;

    /* 显式保存昵称：草稿随调用更新；结果未知恢复期间不允许调用 */
    virtual void saveNickname(const QString &nickname) = 0;

    /* 仅可重试登录失败与资料刷新失败；昵称保存失败必须走显式 saveNickname */
    virtual void retry() = 0;

    /* 退出：立即清快照 + 导航 Login + 尽力通知服务层；迟到响应一律丢弃 */
    virtual void logout() = 0;

signals:
    void flowChanged(const UserFlowSnapshot &snapshot);
    void navigationRequested(NavigationTarget target);
};
