QT += core testlib
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
    $$PROJECT_ROOT/src/network/clientsocketworker.cpp \
    $$PROJECT_ROOT/src/network/clientsocketthreadmanager.cpp

HEADERS += \
    $$PROJECT_ROOT/src/common/connectionstate.h \
    $$PROJECT_ROOT/src/network/ibackendchannel.h \
    $$PROJECT_ROOT/src/network/clientsocketworker.h \
    $$PROJECT_ROOT/src/network/clientsocketthreadmanager.h

