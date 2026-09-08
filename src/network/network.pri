# Network transport sources are maintained by the network owner.
# Business modules depend on typed network interfaces and must not include
# protocol constants or access QTcpSocket directly.

SOURCES += \
    $$PROJECT_ROOT/massagehandler.cpp \
    $$PROJECT_ROOT/src/network/qtnetworktransport.cpp \
    $$PROJECT_ROOT/src/network/backendclient.cpp \
    $$PROJECT_ROOT/src/network/clientsocketworker.cpp \
    $$PROJECT_ROOT/src/network/clientsocketthreadmanager.cpp \
    $$PROJECT_ROOT/src/network/realusernetworkapi.cpp \
    $$PROJECT_ROOT/src/network/realchargerservice.cpp \
    $$PROJECT_ROOT/src/network/realorderservice.cpp

HEADERS += \
    $$PROJECT_ROOT/massagehandler.h \
    $$PROJECT_ROOT/protocol.h \
    $$PROJECT_ROOT/src/network/inetworktransport.h \
    $$PROJECT_ROOT/src/network/ibackendchannel.h \
    $$PROJECT_ROOT/src/network/qtnetworktransport.h \
    $$PROJECT_ROOT/src/network/backendclient.h \
    $$PROJECT_ROOT/src/network/clientsocketworker.h \
    $$PROJECT_ROOT/src/network/clientsocketthreadmanager.h \
    $$PROJECT_ROOT/src/network/realusernetworkapi.h \
    $$PROJECT_ROOT/src/network/realchargerservice.h \
    $$PROJECT_ROOT/src/network/realorderservice.h
