QT += core testlib

CONFIG += c++17 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = map-contract-tests

PROJECT_ROOT = $$PWD/../..

INCLUDEPATH += $$PROJECT_ROOT/src

HEADERS += \
    $$PROJECT_ROOT/src/common/clienterror.h \
    $$PROJECT_ROOT/src/common/requestcontext.h \
    $$PROJECT_ROOT/src/modules/map/maptypes.h \
    $$PROJECT_ROOT/src/modules/map/imapservice.h \
    $$PROJECT_ROOT/src/modules/charger/chargertypes.h \
    $$PROJECT_ROOT/src/modules/charger/ichargerservice.h \
    $$PROJECT_ROOT/src/presentation/contracts/mapviewstates.h \
    $$PROJECT_ROOT/src/app/imapuibinder.h

SOURCES += map-contract-tests.cpp
