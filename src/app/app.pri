# 用户应用装配层：不依赖 QWidget 或具体页面实现。

HEADERS += \
    $$PWD/../presentation/contracts/userviewstates.h \
    $$PWD/../presentation/contracts/profileviewstate.h \
    $$PWD/imapuibinder.h \
    $$PWD/mapuibinder.h \
    $$PWD/icharginguibinder.h \
    $$PWD/charginguibinder.h \
    $$PWD/ichargingsessionuibinder.h \
    $$PWD/chargingsessionuibinder.h \
    $$PWD/walletuibinder.h \
    $$PWD/iuseruibinder.h \
    $$PWD/useruibinder.h \
    $$PWD/application.h

SOURCES += \
    $$PWD/mapuibinder.cpp \
    $$PWD/charginguibinder.cpp \
    $$PWD/chargingsessionuibinder.cpp \
    $$PWD/walletuibinder.cpp \
    $$PWD/useruibinder.cpp \
    $$PWD/application.cpp
