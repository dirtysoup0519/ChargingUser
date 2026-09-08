QT += core testlib
CONFIG += c++17 console testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = wallet-module-tests
PROJECT_ROOT = $$PWD/../..
INCLUDEPATH += $$PROJECT_ROOT/src
include($$PROJECT_ROOT/src/modules/wallet/wallet.pri)
SOURCES += \
    $$PROJECT_ROOT/src/app/walletuibinder.cpp \
    wallet-module-tests.cpp
HEADERS += \
    $$PROJECT_ROOT/src/app/walletuibinder.h \
    $$PROJECT_ROOT/src/presentation/contracts/walletviewstate.h
