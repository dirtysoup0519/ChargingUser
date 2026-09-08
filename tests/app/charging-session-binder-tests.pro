QT += core testlib
CONFIG += c++17 console testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = charging-session-binder-tests
PROJECT_ROOT = $$PWD/../..
INCLUDEPATH += $$PROJECT_ROOT/src
HEADERS += \
    $$PROJECT_ROOT/src/app/ichargingsessionuibinder.h \
    $$PROJECT_ROOT/src/app/chargingsessionuibinder.h \
    $$PROJECT_ROOT/src/modules/order/iorderservice.h
SOURCES += \
    $$PROJECT_ROOT/src/app/chargingsessionuibinder.cpp \
    charging-session-binder-tests.cpp
