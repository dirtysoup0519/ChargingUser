#pragma once

#include <QMetaType>
#include <QString>

enum class ChargeConfirmationStatus
{
    Idle,
    Loading,
    Ready,
    Submitting,
    Error,
    ResultUnknown
};

struct ChargeConfirmationViewState
{
    QString stationId;
    QString chargerId;
    ChargeConfirmationStatus status = ChargeConfirmationStatus::Idle;
    QString stationName;
    QString stationAddress;
    QString chargerCode;
    QString chargerTypeText;
    QString powerText;
    QString chargerStatusText;
    QString energyPriceText;
    QString walletBalanceText;
    QString message;
    QString operationId;
    bool canStart = false;
    bool canRetry = false;
    bool canRecharge = false;
    QString disabledReason;
};

Q_DECLARE_METATYPE(ChargeConfirmationStatus)
Q_DECLARE_METATYPE(ChargeConfirmationViewState)
