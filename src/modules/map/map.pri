# 地图模块新增腾讯 WebService 适配器（QNetworkAccessManager 直连），需要 network 组件
QT += network

HEADERS += \
    $$PWD/coordinateconverter.h \
    $$PWD/imapservice.h \
    $$PWD/maptypes.h \
    $$PWD/mockmapservice.h \
    $$PWD/tencentmapservice.h

SOURCES += \
    $$PWD/coordinateconverter.cpp \
    $$PWD/mockmapservice.cpp \
    $$PWD/tencentmapservice.cpp
