QT += core testlib
CONFIG += c++17 console testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = wallet-module-tests
PROJECT_ROOT = $$PWD/../..
INCLUDEPATH += $$PROJECT_ROOT/src
include($$PROJECT_ROOT/src/modules/wallet/wallet.pri)
SOURCES += wallet-module-tests.cpp
