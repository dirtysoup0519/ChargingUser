# M4 应用流程协调层：无 UI 的用户流程编排（登录/分流/资料处理/权限分流/退出）
# 由 ChargingUser.pro 通过 include() 引入；不依赖 ui/、styles/、resources/
# 本层只依赖 IUserService 及既有业务类型，不接触协议消息码、JSON、Socket

INCLUDEPATH += $$PROJECT_ROOT/src

HEADERS += \
    $$PWD/userflowtypes.h \
    $$PWD/iappflowcoordinator.h \
    $$PWD/appflowcoordinator.h

SOURCES += \
    $$PWD/appflowcoordinator.cpp
