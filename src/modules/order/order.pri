HEADERS += $$PWD/ordertypes.h $$PWD/iorderservice.h

!contains(CONFIG, real_network) {
    HEADERS += $$PWD/mockorderservice.h
    SOURCES += $$PWD/mockorderservice.cpp
}
