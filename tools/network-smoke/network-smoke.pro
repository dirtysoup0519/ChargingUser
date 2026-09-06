QT += core network
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TEMPLATE = app
TARGET = network-smoke

PROJECT_ROOT = $$PWD/../..
INCLUDEPATH += $$PROJECT_ROOT \
               $$PROJECT_ROOT/src

SOURCES += \
    main.cpp \
    $$PROJECT_ROOT/massagehandler.cpp \
    $$PROJECT_ROOT/src/network/qtnetworktransport.cpp

HEADERS += \
    $$PROJECT_ROOT/massagehandler.h \
    $$PROJECT_ROOT/protocol.h \
    $$PROJECT_ROOT/src/network/inetworktransport.h \
    $$PROJECT_ROOT/src/network/qtnetworktransport.h
