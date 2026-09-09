# 独立用户流程 Demo；仅在 qmake CONFIG+=user_demo 时编译。

SOURCES += \
    $$PWD/chargedemofixtureloader.cpp \
    $$PWD/mapdemofixtureloader.cpp \
    $$PWD/reservationdemofixtureloader.cpp \
    $$PWD/userdemomain.cpp \
    $$PWD/userdemocontroller.cpp

HEADERS += \
    $$PWD/chargedemofixtureloader.h \
    $$PWD/mapdemofixtureloader.h \
    $$PWD/reservationdemofixtureloader.h \
    $$PWD/userdemocontroller.h

DISTFILES += \
    $$PWD/charge-demo-data.tmp \
    $$PWD/charging-session-demo-data.tmp \
    $$PWD/map-demo-data.tmp \
    $$PWD/payment-demo-data.tmp \
    $$PWD/order-list-demo-data.tmp \
    $$PWD/reservation-demo-data.tmp \
    $$PWD/user-demo-data.tmp
