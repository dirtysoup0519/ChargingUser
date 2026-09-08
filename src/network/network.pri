# Network transport sources are maintained by the network owner.
# Business modules depend on typed network interfaces and must not include
# protocol constants or access QTcpSocket directly.

# UI Demo uses typed Mock services and must not spend each clean build compiling
# the production socket/protocol adapters. The real_network target keeps the
# complete source list used by the integration owner.
contains(CONFIG, real_network) {
    SOURCES += \
        $$PROJECT_ROOT/massagehandler.cpp \
        $$PROJECT_ROOT/src/network/qtnetworktransport.cpp \
        $$PROJECT_ROOT/src/network/backendclient.cpp \
        $$PROJECT_ROOT/src/network/clientsocketworker.cpp \
        $$PROJECT_ROOT/src/network/clientsocketthreadmanager.cpp \
        $$PROJECT_ROOT/src/network/realusernetworkapi.cpp \
        $$PROJECT_ROOT/src/network/realchargerservice.cpp \
        $$PROJECT_ROOT/src/network/realchargingnetworkapi.cpp \
        $$PROJECT_ROOT/src/network/realreservationservice.cpp \
        $$PROJECT_ROOT/src/network/serverpushdispatcher.cpp \
        $$PROJECT_ROOT/src/network/realorderservice.cpp \
        $$PROJECT_ROOT/src/network/realwalletnetworkapi.cpp

    HEADERS += \
        $$PROJECT_ROOT/massagehandler.h \
        $$PROJECT_ROOT/protocol.h \
        $$PROJECT_ROOT/src/network/inetworktransport.h \
        $$PROJECT_ROOT/src/network/ibackendchannel.h \
        $$PROJECT_ROOT/src/network/qtnetworktransport.h \
        $$PROJECT_ROOT/src/network/backendclient.h \
        $$PROJECT_ROOT/src/network/clientsocketworker.h \
        $$PROJECT_ROOT/src/network/clientsocketthreadmanager.h \
        $$PROJECT_ROOT/src/modules/charger/ichargerservice.h \
        $$PROJECT_ROOT/src/modules/charging/ichargingnetworkapi.h \
        $$PROJECT_ROOT/src/modules/order/iorderservice.h \
        $$PROJECT_ROOT/src/modules/reservation/ireservationservice.h \
        $$PROJECT_ROOT/src/modules/user/iusernetworkapi.h \
        $$PROJECT_ROOT/src/modules/wallet/iwalletnetworkapi.h \
        $$PROJECT_ROOT/src/network/realusernetworkapi.h \
        $$PROJECT_ROOT/src/network/realchargerservice.h \
        $$PROJECT_ROOT/src/network/realchargingnetworkapi.h \
        $$PROJECT_ROOT/src/network/realreservationservice.h \
        $$PROJECT_ROOT/src/network/serverpushdispatcher.h \
        $$PROJECT_ROOT/src/network/realorderservice.h \
        $$PROJECT_ROOT/src/network/realwalletnetworkapi.h
}
