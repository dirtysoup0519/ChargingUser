QT += widgets

CONFIG += c++17
CONFIG -= app_bundle

TEMPLATE = app
TARGET = ChargingUserUI

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
