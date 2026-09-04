QT += core testlib
CONFIG += c++17 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = user-module-tests

PROJECT_ROOT = $$PWD/../..
INCLUDEPATH += $$PROJECT_ROOT/src

SOURCES += \
    user-module-tests.cpp \
    $$PROJECT_ROOT/src/modules/user/mockusernetworkapi.cpp \
    $$PROJECT_ROOT/src/modules/user/userservice.cpp

HEADERS += \
    $$PROJECT_ROOT/src/common/clienterror.h \
    $$PROJECT_ROOT/src/common/operationresult.h \
    $$PROJECT_ROOT/src/common/requestcontext.h \
    $$PROJECT_ROOT/src/modules/user/iusernetworkapi.h \
    $$PROJECT_ROOT/src/modules/user/iuserservice.h \
    $$PROJECT_ROOT/src/modules/user/mockusernetworkapi.h \
    $$PROJECT_ROOT/src/modules/user/userservice.h \
    $$PROJECT_ROOT/src/modules/user/usertypes.h
