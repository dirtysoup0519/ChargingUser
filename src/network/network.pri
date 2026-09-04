# Network transport sources are maintained by the network owner.
# Business modules depend on typed network interfaces and must not include
# protocol constants or access QTcpSocket directly.

SOURCES += \
    $$PROJECT_ROOT/massagehandler.cpp

HEADERS += \
    $$PROJECT_ROOT/massagehandler.h \
    $$PROJECT_ROOT/protocol.h
