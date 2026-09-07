QT = core network testlib

CONFIG += c++17 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = real-charger-service-tests

PROJECT_ROOT = $$PWD/../..

INCLUDEPATH += $$PROJECT_ROOT $$PROJECT_ROOT/src $$PROJECT_ROOT/src/network

include($$PROJECT_ROOT/src/network/network.pri)
include($$PROJECT_ROOT/src/modules/charger/charger.pri)

# network.pri 里的 RealUserNetworkApi 需要 IUserNetworkApi 的 moc，
# 正式构建由 user.pri 提供；本测试不需要 user 域，单独补 moc 输入。
HEADERS += $$PROJECT_ROOT/src/modules/user/iusernetworkapi.h

SOURCES += real-charger-service-tests.cpp
