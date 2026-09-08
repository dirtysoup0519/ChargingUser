HEADERS += \
    $$PWD/chargingtypes.h \
    $$PWD/chargingbackendcapabilities.h \
    $$PWD/ichargingnetworkapi.h \
    $$PWD/ichargingservice.h \
    $$PWD/chargingservice.h

SOURCES += $$PWD/chargingservice.cpp

!contains(CONFIG, real_network) {
    HEADERS += $$PWD/mockchargingservice.h
    SOURCES += $$PWD/mockchargingservice.cpp
} else {
    # 阶段 B 占位：真实入口页面可达但显式失败，不产生伪数据；
    # 阶段 F 的 RealChargingNetworkApi 就绪后删除。
    HEADERS += $$PWD/placeholderchargingnetworkapi.h
    SOURCES += $$PWD/placeholderchargingnetworkapi.cpp
}
