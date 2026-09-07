QT += core testlib

CONFIG += c++17 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = m2-logic-tests

PROJECT_ROOT = $$PWD/../..

INCLUDEPATH += $$PROJECT_ROOT/src

include($$PROJECT_ROOT/src/modules/map/map.pri)
include($$PROJECT_ROOT/src/modules/charger/charger.pri)

HEADERS += \
    $$PROJECT_ROOT/src/common/clienterror.h \
    $$PROJECT_ROOT/src/common/requestcontext.h

SOURCES += m2-logic-tests.cpp
