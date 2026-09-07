QT = core network testlib

CONFIG += c++17 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = tencent-map-service-tests

PROJECT_ROOT = $$PWD/../..

INCLUDEPATH += $$PROJECT_ROOT/src

include($$PROJECT_ROOT/src/modules/map/map.pri)

HEADERS += \
    $$PROJECT_ROOT/src/common/clienterror.h \
    $$PROJECT_ROOT/src/common/requestcontext.h

SOURCES += tencent-map-service-tests.cpp
