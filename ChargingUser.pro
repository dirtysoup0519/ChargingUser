QT += widgets network

packagesExist(zxing) {
    CONFIG += link_pkgconfig
    PKGCONFIG += zxing
    DEFINES += CHARGINGUSER_ENABLE_ZXING
}

# WebEngine/WebChannel are optional: local Qt kits without these modules must
# still start with the existing painter-based fallback map.
qtHaveModule(webenginewidgets):qtHaveModule(webchannel) {
    QT += webenginewidgets webchannel
    DEFINES += CHARGINGUSER_ENABLE_TENCENT_WEBMAP
}

CONFIG += c++17
CONFIG -= app_bundle

# Normal builds must use the real server. The fixture-backed demo is opt-in so
# running qmake again cannot silently switch production data back to *.tmp.
!contains(CONFIG, real_network):!contains(CONFIG, user_demo) {
    CONFIG += real_network
}

TEMPLATE = app
TARGET = ChargingUserUI

PROJECT_ROOT = $$PWD

# Real client keeps the documented ./bin/ChargingUserUI path. An explicitly
# requested fixture demo is isolated under its own build directory so it can
# never overwrite the real executable.
contains(CONFIG, user_demo) {
    DESTDIR = $$OUT_PWD/bin
    MOC_DIR = $$OUT_PWD/build/moc
    UI_DIR = $$OUT_PWD/build/ui
    RCC_DIR = $$OUT_PWD/build/rcc
    OBJECTS_DIR = $$OUT_PWD/build/obj
} else {
    DESTDIR = $$PROJECT_ROOT/bin
    MOC_DIR = $$PROJECT_ROOT/build/moc
    UI_DIR = $$PROJECT_ROOT/build/ui
    RCC_DIR = $$PROJECT_ROOT/build/rcc
    OBJECTS_DIR = $$PROJECT_ROOT/build/obj
}

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
include($$PROJECT_ROOT/src/modules/charging/charging.pri)
include($$PROJECT_ROOT/src/modules/order/order.pri)
include($$PROJECT_ROOT/src/modules/wallet/wallet.pri)
include($$PROJECT_ROOT/src/modules/reservation/reservation.pri)
include($$PROJECT_ROOT/src/modules/user/user.pri)
include($$PROJECT_ROOT/src/flow/flow.pri)
include($$PROJECT_ROOT/src/app/app.pri)

contains(CONFIG, user_demo) {
    contains(CONFIG, real_network) {
        error("user_demo and real_network cannot be enabled together")
    }
    # Demo 入口替换正式入口；userdemomain.cpp 在 demo.pri 中登记。
    SOURCES -= src/main.cpp
    RESOURCES += resources/demo-resources.qrc
    include($$PROJECT_ROOT/src/demo/demo.pri)
}
# 默认构建（real_network）直接以 src/main.cpp 作为正式入口。
