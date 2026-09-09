QT += widgets testlib
CONFIG += c++17 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = order-list-window-tests

PROJECT_ROOT = $$PWD/../..
INCLUDEPATH += $$PROJECT_ROOT \
               $$PROJECT_ROOT/src

SOURCES += \
    order-list-window-tests.cpp \
    $$PROJECT_ROOT/src/presentation/pages/profile/orderlistwindow.cpp

HEADERS += \
    $$PROJECT_ROOT/src/presentation/pages/profile/orderlistwindow.h \
    $$PROJECT_ROOT/src/presentation/contracts/orderlistviewstate.h
