QT += testlib network
CONFIG += testcase c++17
TEMPLATE = app
TARGET = reservation-ui-binder-tests

PROJECT_ROOT = $$PWD/../..
INCLUDEPATH += $$PROJECT_ROOT $$PROJECT_ROOT/src

SOURCES += \
    $$PROJECT_ROOT/src/app/reservationuibinder.cpp \
    reservation-ui-binder-tests.cpp

HEADERS += \
    $$PROJECT_ROOT/src/app/reservationuibinder.h \
    $$PROJECT_ROOT/src/common/clienterror.h \
    $$PROJECT_ROOT/src/common/requestcontext.h \
    $$PROJECT_ROOT/src/modules/reservation/ireservationservice.h \
    $$PROJECT_ROOT/src/modules/reservation/reservationtypes.h \
    $$PROJECT_ROOT/src/presentation/contracts/reservationviewstates.h
