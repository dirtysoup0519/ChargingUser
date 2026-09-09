QT += testlib network
CONFIG += testcase c++17
TEMPLATE = app
TARGET = real-wallet-network-tests

PROJECT_ROOT = $$PWD/../..
INCLUDEPATH += $$PROJECT_ROOT $$PROJECT_ROOT/src $$PROJECT_ROOT/src/network

SOURCES += \
    $$PROJECT_ROOT/massagehandler.cpp \
    $$PROJECT_ROOT/src/network/backendclient.cpp \
    $$PROJECT_ROOT/src/network/realwalletnetworkapi.cpp \
    real-wallet-network-tests.cpp

HEADERS += \
    $$PROJECT_ROOT/massagehandler.h \
    $$PROJECT_ROOT/protocol.h \
    $$PROJECT_ROOT/src/network/inetworktransport.h \
    $$PROJECT_ROOT/src/network/ibackendchannel.h \
    $$PROJECT_ROOT/src/network/backendclient.h \
    $$PROJECT_ROOT/src/network/realwalletnetworkapi.h \
    $$PROJECT_ROOT/src/modules/wallet/iwalletnetworkapi.h \
    $$PROJECT_ROOT/src/modules/wallet/walletbackendcapabilities.h \
    $$PROJECT_ROOT/src/modules/wallet/wallettypes.h
