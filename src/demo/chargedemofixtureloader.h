#pragma once

#include "presentation/contracts/chargingviewstates.h"

#include <QString>

bool loadChargeConfirmationDemo(const QString &resourcePath,
                                ChargeConfirmationViewState *state,
                                QString *errorMessage = nullptr);
