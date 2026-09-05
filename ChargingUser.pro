QT += widgets

CONFIG += c++17
CONFIG -= app_bundle

TEMPLATE = app
TARGET = ChargingUserUI

SOURCES += src/main.cpp

include(src/presentation/presentation.pri)

FORMS += \
    ui/loginwindow.ui \
    ui/profileeditwindow.ui \
    ui/mainwindow.ui \
    ui/stationdetailwindow.ui \
    ui/navigationwindow.ui

RESOURCES += \
    resources/resources.qrc
