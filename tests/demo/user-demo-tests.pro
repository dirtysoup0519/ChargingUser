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
include($$PROJECT_ROOT/src/modules/user/user.pri)
include($$PROJECT_ROOT/src/flow/flow.pri)
include($$PROJECT_ROOT/src/app/app.pri)

SOURCES += \
    user-demo-tests.cpp \
    $$PROJECT_ROOT/src/demo/userdemocontroller.cpp

HEADERS += \
    $$PROJECT_ROOT/src/demo/userdemocontroller.h

FORMS += \
    $$PROJECT_ROOT/ui/auth/loginwindow.ui \
    $$PROJECT_ROOT/ui/profile/profileeditwindow.ui \
    $$PROJECT_ROOT/ui/shell/mainwindow.ui \
    $$PROJECT_ROOT/ui/home/stationdetailwindow.ui \
    $$PROJECT_ROOT/ui/home/navigationwindow.ui \
    $$PROJECT_ROOT/ui/profile/walletrechargewindow.ui

RESOURCES += \
    $$PROJECT_ROOT/resources/resources.qrc
