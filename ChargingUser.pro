QT += widgets network

CONFIG += c++17
CONFIG -= app_bundle

TEMPLATE = app
TARGET = ChargingUserUI

PROJECT_ROOT = $$PWD

DESTDIR = $$PROJECT_ROOT/bin
MOC_DIR = $$PROJECT_ROOT/build/moc
UI_DIR = $$PROJECT_ROOT/build/ui
RCC_DIR = $$PROJECT_ROOT/build/rcc
OBJECTS_DIR = $$PROJECT_ROOT/build/obj

INCLUDEPATH += \
    $$PROJECT_ROOT \
    $$PROJECT_ROOT/src

SOURCES += \
    src/main.cpp \
    src/loginwindow.cpp \
    src/profileeditwindow.cpp \
    src/stationdetailwindow.cpp \
    src/navigationwindow.cpp \
    src/mainwindow.cpp

HEADERS += \
    src/loginwindow.h \
    src/profileeditwindow.h \
    src/stationdetailwindow.h \
    src/navigationwindow.h \
    src/mainwindow.h

FORMS += \
    ui/loginwindow.ui \
    ui/profileeditwindow.ui \
    ui/mainwindow.ui \
    ui/stationdetailwindow.ui \
    ui/navigationwindow.ui

RESOURCES += \
    resources/resources.qrc

include($$PROJECT_ROOT/src/network/network.pri)
include($$PROJECT_ROOT/src/modules/user/user.pri)
include($$PROJECT_ROOT/src/flow/flow.pri)
include($$PROJECT_ROOT/src/app/app.pri)
