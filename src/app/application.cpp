#include "app/application.h"

#include "app/useruibinder.h"
#include "flow/appflowcoordinator.h"
#include "modules/user/iusernetworkapi.h"
#include "modules/user/userservice.h"

UserApplicationAssembly::UserApplicationAssembly(IUserNetworkApi *userNetworkApi,
                                                 QObject *parent)
    : QObject(parent)
    , m_userService(nullptr)
    , m_flowCoordinator(nullptr)
    , m_userUiBinder(nullptr)
{
    Q_ASSERT(userNetworkApi);

    m_userService = new UserService(userNetworkApi, this);
    m_flowCoordinator = new AppFlowCoordinator(m_userService, this);
    m_userUiBinder = new UserUiBinder(m_userService, m_flowCoordinator, this);
}

IUserService *UserApplicationAssembly::userService() const
{
    return m_userService;
}

IAppFlowCoordinator *UserApplicationAssembly::flowCoordinator() const
{
    return m_flowCoordinator;
}

IUserUiBinder *UserApplicationAssembly::userUiBinder() const
{
    return m_userUiBinder;
}
