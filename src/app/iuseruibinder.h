#pragma once

#include "flow/userflowtypes.h"
#include "presentation/contracts/userviewstates.h"

#include <QObject>
#include <QString>

/* UI 与用户业务的稳定边界。
 * 页面把语义化意图连接到这些槽，并通过状态信号调用自身 render()；
 * 接口不引用 QWidget、具体页面类、objectName 或页面编号。
 */
class IUserUiBinder : public QObject
{
    Q_OBJECT

public:
    explicit IUserUiBinder(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~IUserUiBinder() override = default;

    virtual LoginViewState currentLoginViewState() const = 0;
    virtual ProfileEditViewState currentProfileEditViewState() const = 0;
    virtual ProfileViewState currentProfileViewState() const = 0;

public slots:
    virtual void loginRequested(const QString &phone) = 0;
    virtual void usernamePasswordLoginRequested(const QString &, const QString &) {}
    virtual void profileSaveRequested(const QString &nickname) = 0;
    virtual void avatarUpdateRequested(const QString &avatarDataUri) = 0;
    virtual void retryRequested() = 0;
    virtual void logoutRequested() = 0;

signals:
    void loginViewStateChanged(const LoginViewState &state);
    void profileEditViewStateChanged(const ProfileEditViewState &state);
    void profileViewStateChanged(const ProfileViewState &state);
    void navigationRequested(NavigationTarget target);
};
