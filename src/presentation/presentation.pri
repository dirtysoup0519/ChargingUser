INCLUDEPATH += $$PWD/contracts $$PWD/widgets

HEADERS += \
    $$PWD/contracts/submitstate.h \
    $$PWD/contracts/loginviewstate.h \
    $$PWD/contracts/profileeditviewstate.h \
    $$PWD/contracts/profileviewstate.h \
    $$PWD/contracts/chargingviewstates.h \
    $$PWD/contracts/chargingsessionviewstate.h \
    $$PWD/contracts/paymentviewstates.h \
    $$PWD/contracts/orderlistviewstate.h \
    $$PWD/contracts/reservationviewstates.h \
    $$PWD/contracts/scanviewstate.h

include($$PWD/widgets/common/common.pri)
include($$PWD/pages/shell/shell.pri)
include($$PWD/pages/auth/auth.pri)
include($$PWD/pages/home/home.pri)
include($$PWD/pages/charging/charging.pri)
include($$PWD/pages/profile/profile.pri)
