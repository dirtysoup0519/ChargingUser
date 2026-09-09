# M1 user identity, profile, and session sources are registered here.
# Wallet and recharge sources belong to M3 and must not be added to this file.

HEADERS += \
    $$PROJECT_ROOT/src/common/clienterror.h \
    $$PROJECT_ROOT/src/common/connectionstate.h \
    $$PROJECT_ROOT/src/common/operationresult.h \
    $$PROJECT_ROOT/src/common/requestcontext.h \
    $$PROJECT_ROOT/src/modules/user/iusernetworkapi.h \
    $$PROJECT_ROOT/src/modules/user/iuserservice.h \
    $$PROJECT_ROOT/src/modules/user/userservice.h \
    $$PROJECT_ROOT/src/modules/user/usertypes.h

SOURCES += \
    $$PROJECT_ROOT/src/modules/user/userservice.cpp

!contains(CONFIG, real_network) {
    HEADERS += $$PROJECT_ROOT/src/modules/user/mockusernetworkapi.h
    SOURCES += $$PROJECT_ROOT/src/modules/user/mockusernetworkapi.cpp
}
