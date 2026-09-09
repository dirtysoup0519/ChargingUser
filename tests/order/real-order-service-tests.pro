QT = core network testlib

CONFIG += c++17 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = real-order-service-tests

PROJECT_ROOT = $$PWD/../..

INCLUDEPATH += $$PROJECT_ROOT $$PROJECT_ROOT/src $$PROJECT_ROOT/src/network

# 本测试只链接订单适配器的直接依赖，避免 network.pri 新增其他模块时
# 把无关 QObject 接口带入，造成缺少对应 moc 的 vtable 链接错误。
SOURCES += \
    $$PROJECT_ROOT/massagehandler.cpp \
    $$PROJECT_ROOT/src/network/backendclient.cpp \
    $$PROJECT_ROOT/src/network/realorderservice.cpp \
    real-order-service-tests.cpp

HEADERS += \
    $$PROJECT_ROOT/massagehandler.h \
    $$PROJECT_ROOT/protocol.h \
    $$PROJECT_ROOT/src/network/inetworktransport.h \
    $$PROJECT_ROOT/src/network/backendclient.h \
    $$PROJECT_ROOT/src/network/realorderservice.h \
    $$PROJECT_ROOT/src/modules/order/iorderservice.h \
    $$PROJECT_ROOT/src/modules/order/ordertypes.h
