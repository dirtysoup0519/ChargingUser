#pragma once

#include "presentation/contracts/chargingviewstates.h"
#include "presentation/contracts/chargingsessionviewstate.h"

#include <QString>

bool loadChargeConfirmationDemo(const QString &resourcePath,
                                ChargeConfirmationViewState *state,
                                QString *errorMessage = nullptr);

bool loadChargingSessionDemo(const QString &resourcePath,
                             QList<ChargingSessionViewState> *sessions,
                             QString *errorMessage = nullptr);
