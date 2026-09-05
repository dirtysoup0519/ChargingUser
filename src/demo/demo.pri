# 独立用户流程 Demo；仅在 qmake CONFIG+=user_demo 时编译。

SOURCES += \
    $$PWD/userdemomain.cpp \
    $$PWD/userdemocontroller.cpp

HEADERS += \
    $$PWD/userdemocontroller.h

DISTFILES += \
    $$PWD/user-demo-data.tmp
