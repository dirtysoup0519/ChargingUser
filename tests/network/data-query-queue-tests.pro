QT += core testlib network
QT -= gui
CONFIG += c++17 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = data-query-queue-tests

PROJECT_ROOT = $$PWD/../..
INCLUDEPATH += $$PROJECT_ROOT \
               $$PROJECT_ROOT/src

SOURCES += \
    data-query-queue-tests.cpp \
    $$PROJECT_ROOT/massagehandler.cpp \
    $$PROJECT_ROOT/src/network/backendclient.cpp

HEADERS += \
    $$PROJECT_ROOT/massagehandler.h \
    $$PROJECT_ROOT/protocol.h \
    $$PROJECT_ROOT/src/network/inetworktransport.h \
    $$PROJECT_ROOT/src/network/backendclient.h \
    $$PROJECT_ROOT/src/common/connectionstate.h
