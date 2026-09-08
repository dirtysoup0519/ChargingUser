HEADERS += \
    $$PWD/wallettypes.h \
    $$PWD/walletbackendcapabilities.h \
    $$PWD/iwalletservice.h \
    $$PWD/iwalletnetworkapi.h \
    $$PWD/walletservice.h
SOURCES += $$PWD/walletservice.cpp

!contains(CONFIG, real_network) {
    HEADERS += $$PWD/mockwalletservice.h
    SOURCES += $$PWD/mockwalletservice.cpp
}
