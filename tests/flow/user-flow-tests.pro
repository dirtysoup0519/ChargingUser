QT += core testlib
CONFIG += c++17 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = user-flow-tests

PROJECT_ROOT = $$PWD/../..
INCLUDEPATH += $$PROJECT_ROOT \
               $$PROJECT_ROOT/src

# 被测对象：AppFlowCoordinator + 真实 UserService + MockUserNetworkApi
# （规格要求：不创建 QApplication/窗口，不依赖真实服务器）
SOURCES += \
    user-flow-tests.cpp \
    $$PROJECT_ROOT/src/flow/appflowcoordinator.cpp \
    $$PROJECT_ROOT/src/modules/user/mockusernetworkapi.cpp \
    $$PROJECT_ROOT/src/modules/user/userservice.cpp

HEADERS += \
    $$PROJECT_ROOT/src/flow/userflowtypes.h \
    $$PROJECT_ROOT/src/flow/iappflowcoordinator.h \
    $$PROJECT_ROOT/src/flow/appflowcoordinator.h \
    $$PROJECT_ROOT/src/common/clienterror.h \
    $$PROJECT_ROOT/src/common/operationresult.h \
    $$PROJECT_ROOT/src/common/requestcontext.h \
    $$PROJECT_ROOT/src/modules/user/iusernetworkapi.h \
    $$PROJECT_ROOT/src/modules/user/iuserservice.h \
    $$PROJECT_ROOT/src/modules/user/mockusernetworkapi.h \
    $$PROJECT_ROOT/src/modules/user/userservice.h \
    $$PROJECT_ROOT/src/modules/user/usertypes.h
