QT += core testlib
CONFIG += c++17 console testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = order-module-tests
PROJECT_ROOT = $$PWD/../..
INCLUDEPATH += $$PROJECT_ROOT/src
include($$PROJECT_ROOT/src/modules/order/order.pri)
SOURCES += order-module-tests.cpp
