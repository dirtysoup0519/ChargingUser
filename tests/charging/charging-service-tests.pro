QT += core testlib
CONFIG += c++17 console testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = charging-service-tests
PROJECT_ROOT = $$PWD/../..
INCLUDEPATH += $$PROJECT_ROOT/src
include($$PROJECT_ROOT/src/modules/charging/charging.pri)
SOURCES += charging-service-tests.cpp
