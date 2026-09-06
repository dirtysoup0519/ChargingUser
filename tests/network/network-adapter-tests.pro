QT += core testlib network
QT -= gui
CONFIG += c++17 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = network-adapter-tests

PROJECT_ROOT = $$PWD/../..
INCLUDEPATH += $$PROJECT_ROOT \
               $$PROJECT_ROOT/src

SOURCES += \
    network-adapter-tests.cpp \
    $$PROJECT_ROOT/massagehandler.cpp \
    $$PROJECT_ROOT/src/network/backendclient.cpp \
    $$PROJECT_ROOT/src/network/realusernetworkapi.cpp

HEADERS += \
    $$PROJECT_ROOT/massagehandler.h \
    $$PROJECT_ROOT/protocol.h \
    $$PROJECT_ROOT/src/network/inetworktransport.h \
    $$PROJECT_ROOT/src/network/backendclient.h \
    $$PROJECT_ROOT/src/network/realusernetworkapi.h \
    $$PROJECT_ROOT/src/common/clienterror.h \
    $$PROJECT_ROOT/src/common/connectionstate.h \
    $$PROJECT_ROOT/src/common/operationresult.h \
    $$PROJECT_ROOT/src/common/requestcontext.h \
    $$PROJECT_ROOT/src/modules/user/iusernetworkapi.h \
    $$PROJECT_ROOT/src/modules/user/usertypes.h
