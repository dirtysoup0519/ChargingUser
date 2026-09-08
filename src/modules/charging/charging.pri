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
}
