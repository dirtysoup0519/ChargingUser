# 地图模块新增腾讯 WebService 适配器（QNetworkAccessManager 直连），需要 network 组件
QT += network

HEADERS += \
    $$PWD/coordinateconverter.h \
    $$PWD/imapservice.h \
    $$PWD/maptypes.h \
    $$PWD/tencentmapservice.h

# WebChannel bridge is QtCore-only and therefore remains buildable on kits
# without WebEngine/WebChannel. The WebEngine widget is enabled separately
# when the target kit provides those modules.
HEADERS += \
    $$PROJECT_ROOT/src/presentation/widgets/map/tencentmapbridge.h

SOURCES += \
    $$PWD/coordinateconverter.cpp \
    $$PWD/tencentmapservice.cpp

!contains(CONFIG, real_network) {
    HEADERS += $$PWD/mockmapservice.h
    SOURCES += $$PWD/mockmapservice.cpp
}

SOURCES += \
    $$PROJECT_ROOT/src/presentation/widgets/map/tencentmapbridge.cpp
