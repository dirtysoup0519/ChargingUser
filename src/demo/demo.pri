# 独立用户流程 Demo；仅在 qmake CONFIG+=user_demo 时编译。

SOURCES += \
    $$PWD/mapdemofixtureloader.cpp \
    $$PWD/userdemomain.cpp \
    $$PWD/userdemocontroller.cpp

HEADERS += \
    $$PWD/mapdemofixtureloader.h \
    $$PWD/userdemocontroller.h

DISTFILES += \
    $$PWD/map-demo-data.tmp \
    $$PWD/user-demo-data.tmp
