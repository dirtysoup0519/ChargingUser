QT = core network testlib

CONFIG += c++17 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = real-charger-service-tests

PROJECT_ROOT = $$PWD/../..

INCLUDEPATH += $$PROJECT_ROOT $$PROJECT_ROOT/src $$PROJECT_ROOT/src/network

include($$PROJECT_ROOT/src/network/network.pri)
include($$PROJECT_ROOT/src/modules/charger/charger.pri)

# network.pri 的 RealUserNetworkApi/RealOrderService 分别继承 IUserNetworkApi
# 与 IOrderService（charger/order/user 域接口）：本测试不需要这些模块其余
# 部分，单独补 moc 输入。
HEADERS += \
    $$PROJECT_ROOT/src/modules/user/iusernetworkapi.h \
    $$PROJECT_ROOT/src/modules/order/iorderservice.h

SOURCES += real-charger-service-tests.cpp
