HEADERS += \
    $$PWD/chargertypes.h \
    $$PWD/ichargerservice.h

!contains(CONFIG, real_network) {
    HEADERS += $$PWD/mockchargerservice.h
    SOURCES += $$PWD/mockchargerservice.cpp
}
