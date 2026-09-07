QT += core testlib

CONFIG += c++17 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = map-ui-binder-tests

PROJECT_ROOT = $$PWD/../..

INCLUDEPATH += $$PROJECT_ROOT/src

include($$PROJECT_ROOT/src/modules/map/map.pri)
include($$PROJECT_ROOT/src/modules/charger/charger.pri)

HEADERS += \
    $$PROJECT_ROOT/src/app/imapuibinder.h \
    $$PROJECT_ROOT/src/app/mapuibinder.h \
    $$PROJECT_ROOT/src/common/clienterror.h \
    $$PROJECT_ROOT/src/common/requestcontext.h \
    $$PROJECT_ROOT/src/presentation/contracts/mapviewstates.h

SOURCES += \
    $$PROJECT_ROOT/src/app/mapuibinder.cpp \
    map-ui-binder-tests.cpp
