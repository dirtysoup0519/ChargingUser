QT += core testlib network
QT -= gui

CONFIG += c++17 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = client-socket-thread-tests

PROJECT_ROOT = $$PWD/../..
INCLUDEPATH += $$PROJECT_ROOT \
               $$PROJECT_ROOT/src

SOURCES += \
    client-socket-thread-tests.cpp \
    $$PROJECT_ROOT/massagehandler.cpp \
    $$PROJECT_ROOT/src/network/backendclient.cpp \
    $$PROJECT_ROOT/src/network/qtnetworktransport.cpp \
    $$PROJECT_ROOT/src/network/clientsocketworker.cpp \
    $$PROJECT_ROOT/src/network/clientsocketthreadmanager.cpp

HEADERS += \
    $$PROJECT_ROOT/massagehandler.h \
    $$PROJECT_ROOT/protocol.h \
    $$PROJECT_ROOT/src/common/connectionstate.h \
    $$PROJECT_ROOT/src/network/ibackendchannel.h \
    $$PROJECT_ROOT/src/network/inetworktransport.h \
    $$PROJECT_ROOT/src/network/backendclient.h \
    $$PROJECT_ROOT/src/network/qtnetworktransport.h \
    $$PROJECT_ROOT/src/network/clientsocketworker.h \
    $$PROJECT_ROOT/src/network/clientsocketthreadmanager.h
