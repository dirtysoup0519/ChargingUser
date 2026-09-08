QT = core network testlib

CONFIG += c++17 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = real-order-service-tests

PROJECT_ROOT = $$PWD/../..

INCLUDEPATH += $$PROJECT_ROOT $$PROJECT_ROOT/src $$PROJECT_ROOT/src/network

include($$PROJECT_ROOT/src/network/network.pri)
include($$PROJECT_ROOT/src/modules/order/order.pri)

# network.pri 的 RealChargerService 继承 IChargerService（charger 域）、
# RealUserNetworkApi 继承 IUserNetworkApi（user 域）：本测试不需要两个
# 模块其余部分，单独补 moc 输入。
HEADERS += \
    $$PROJECT_ROOT/src/modules/charger/ichargerservice.h \
    $$PROJECT_ROOT/src/modules/user/iusernetworkapi.h

SOURCES += real-order-service-tests.cpp
