QT += widgets

CONFIG += c++17
CONFIG -= app_bundle

TEMPLATE = app
TARGET = ChargingUserUI

SOURCES += src/main.cpp

include(src/presentation/presentation.pri)

FORMS += \
    ui/shell/mainwindow.ui \
    ui/auth/loginwindow.ui \
    ui/home/stationdetailwindow.ui \
    ui/home/navigationwindow.ui \
    ui/profile/profileeditwindow.ui \
    ui/profile/walletrechargewindow.ui

RESOURCES += \
    resources/resources.qrc
