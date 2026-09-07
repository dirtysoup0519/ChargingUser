QT += core testlib

CONFIG += c++17 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = charging-ui-binder-tests

PROJECT_ROOT = $$PWD/../..
INCLUDEPATH += $$PROJECT_ROOT/src

HEADERS += \
    $$PROJECT_ROOT/src/common/clienterror.h \
    $$PROJECT_ROOT/src/common/requestcontext.h \
    $$PROJECT_ROOT/src/modules/charging/chargingtypes.h \
    $$PROJECT_ROOT/src/modules/charging/ichargingservice.h \
    $$PROJECT_ROOT/src/presentation/contracts/chargingviewstates.h \
    $$PROJECT_ROOT/src/app/icharginguibinder.h \
    $$PROJECT_ROOT/src/app/charginguibinder.h

SOURCES += \
    $$PROJECT_ROOT/src/app/charginguibinder.cpp \
    charging-ui-binder-tests.cpp
