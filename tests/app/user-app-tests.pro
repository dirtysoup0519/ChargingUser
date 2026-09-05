QT += core testlib
QT -= gui

CONFIG += console c++17 testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = user-app-tests

PROJECT_ROOT = ../..

INCLUDEPATH += \
    $$PROJECT_ROOT \
    $$PROJECT_ROOT/src

SOURCES += \
    user-app-tests.cpp \
    $$PROJECT_ROOT/src/app/application.cpp \
    $$PROJECT_ROOT/src/app/useruibinder.cpp \
    $$PROJECT_ROOT/src/flow/appflowcoordinator.cpp \
    $$PROJECT_ROOT/src/modules/user/userservice.cpp \
    $$PROJECT_ROOT/src/modules/user/mockusernetworkapi.cpp

HEADERS += \
    $$PROJECT_ROOT/src/app/application.h \
    $$PROJECT_ROOT/src/app/iuseruibinder.h \
    $$PROJECT_ROOT/src/app/useruibinder.h \
    $$PROJECT_ROOT/src/presentation/contracts/userviewstates.h \
    $$PROJECT_ROOT/src/flow/appflowcoordinator.h \
    $$PROJECT_ROOT/src/flow/iappflowcoordinator.h \
    $$PROJECT_ROOT/src/flow/userflowtypes.h \
    $$PROJECT_ROOT/src/modules/user/iusernetworkapi.h \
    $$PROJECT_ROOT/src/modules/user/iuserservice.h \
    $$PROJECT_ROOT/src/modules/user/mockusernetworkapi.h \
    $$PROJECT_ROOT/src/modules/user/userservice.h \
    $$PROJECT_ROOT/src/modules/user/usertypes.h
