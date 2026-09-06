QT += core testlib
QT -= gui

CONFIG += c++17 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = protocol-framing-tests

PROJECT_ROOT = $$PWD/../..
INCLUDEPATH += $$PROJECT_ROOT

SOURCES += \
    protocol-framing-tests.cpp \
    $$PROJECT_ROOT/massagehandler.cpp

HEADERS += \
    $$PROJECT_ROOT/massagehandler.h \
    $$PROJECT_ROOT/protocol.h
