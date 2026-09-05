# 用户应用装配层：不依赖 QWidget 或具体页面实现。

HEADERS += \
    $$PWD/../presentation/contracts/userviewstates.h \
    $$PWD/iuseruibinder.h \
    $$PWD/useruibinder.h \
    $$PWD/application.h

SOURCES += \
    $$PWD/useruibinder.cpp \
    $$PWD/application.cpp
