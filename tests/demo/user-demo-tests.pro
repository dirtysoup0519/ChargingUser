QT += core gui widgets testlib

CONFIG += console c++17 testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = user-demo-tests

PROJECT_ROOT = ../..

INCLUDEPATH += \
    $$PROJECT_ROOT \
    $$PROJECT_ROOT/src

include($$PROJECT_ROOT/src/presentation/presentation.pri)
include($$PROJECT_ROOT/src/modules/map/map.pri)
include($$PROJECT_ROOT/src/modules/charger/charger.pri)
include($$PROJECT_ROOT/src/modules/charging/charging.pri)
include($$PROJECT_ROOT/src/modules/order/order.pri)
include($$PROJECT_ROOT/src/modules/wallet/wallet.pri)
include($$PROJECT_ROOT/src/modules/user/user.pri)
include($$PROJECT_ROOT/src/flow/flow.pri)
include($$PROJECT_ROOT/src/app/app.pri)

SOURCES += \
    user-demo-tests.cpp \
    $$PROJECT_ROOT/src/demo/chargedemofixtureloader.cpp \
    $$PROJECT_ROOT/src/demo/reservationdemofixtureloader.cpp \
    $$PROJECT_ROOT/src/demo/userdemocontroller.cpp

HEADERS += \
    $$PROJECT_ROOT/src/demo/chargedemofixtureloader.h \
    $$PROJECT_ROOT/src/demo/reservationdemofixtureloader.h \
    $$PROJECT_ROOT/src/demo/userdemocontroller.h

# userdemocontroller 读取 :/demo/*.tmp 夹具；正式构建的 demo 资源
# 已隔离到 demo-resources.qrc，测试工程需显式携带。
RESOURCES += $$PROJECT_ROOT/resources/demo-resources.qrc

FORMS += \
    $$PROJECT_ROOT/ui/auth/loginwindow.ui \
    $$PROJECT_ROOT/ui/profile/profileeditwindow.ui \
    $$PROJECT_ROOT/ui/shell/mainwindow.ui \
    $$PROJECT_ROOT/ui/home/stationdetailwindow.ui \
    $$PROJECT_ROOT/ui/home/navigationwindow.ui \
    $$PROJECT_ROOT/ui/profile/walletrechargewindow.ui

RESOURCES += \
    $$PROJECT_ROOT/resources/resources.qrc
