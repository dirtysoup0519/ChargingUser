#pragma once

#include <QObject>

class IUserNetworkApi;
class IUserService;
class IAppFlowCoordinator;
class IUserUiBinder;

/* 用户功能对象图的无 UI 装配接口。
 * 调用方负责提供 IUserNetworkApi，装配对象拥有其余三层对象；网络接口必须
 * 比装配对象存活更久。正式 main.cpp 与 UI 演示入口可以复用同一对象图。
 */
class IUserApplicationAssembly
{
public:
    virtual ~IUserApplicationAssembly() = default;

    virtual IUserService *userService() const = 0;
    virtual IAppFlowCoordinator *flowCoordinator() const = 0;
    virtual IUserUiBinder *userUiBinder() const = 0;
};

class UserApplicationAssembly final : public QObject,
                                      public IUserApplicationAssembly
{
    Q_OBJECT

public:
    explicit UserApplicationAssembly(IUserNetworkApi *userNetworkApi,
                                     QObject *parent = nullptr);

    IUserService *userService() const override;
    IAppFlowCoordinator *flowCoordinator() const override;
    IUserUiBinder *userUiBinder() const override;

private:
    IUserService *m_userService;
    IAppFlowCoordinator *m_flowCoordinator;
    IUserUiBinder *m_userUiBinder;
};
