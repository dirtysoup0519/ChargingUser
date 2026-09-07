QT += widgets network

# WebEngine/WebChannel are optional: local Qt kits without these modules must
# still start with the existing painter-based fallback map.
qtHaveModule(webenginewidgets):qtHaveModule(webchannel) {
    QT += webenginewidgets webchannel
    DEFINES += CHARGINGUSER_ENABLE_TENCENT_WEBMAP
}

CONFIG += c++17
CONFIG -= app_bundle

# Local/Qt Creator builds should exercise the complete UI assembly by default.
# Production network integration remains an explicit CONFIG+=real_network mode.
!contains(CONFIG, real_network) {
    CONFIG += user_demo
}

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
    src/main.cpp

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

include($$PROJECT_ROOT/src/network/network.pri)
include($$PROJECT_ROOT/src/modules/map/map.pri)
include($$PROJECT_ROOT/src/modules/charger/charger.pri)
include($$PROJECT_ROOT/src/modules/user/user.pri)
include($$PROJECT_ROOT/src/flow/flow.pri)
include($$PROJECT_ROOT/src/app/app.pri)

contains(CONFIG, user_demo) {
    contains(CONFIG, real_network) {
        error("user_demo and real_network cannot be enabled together")
    }
    SOURCES -= src/main.cpp
    include($$PROJECT_ROOT/src/demo/demo.pri)
} else:contains(CONFIG, real_network) {
    SOURCES -= src/main.cpp
    SOURCES += src/realnetworkmain.cpp
}
