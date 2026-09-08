#pragma once

#include "settlementviewstate.h"

#include <QString>
#include <QMetaType>

enum class PaymentPurpose { ChargingSettlement, Reservation };
enum class PaymentViewStatus { Ready, Submitting, Success, Error, ResultUnknown };

struct PaymentViewState
{
    QString businessId;
    PaymentPurpose purpose = PaymentPurpose::ChargingSettlement;
    QString titleText;
    QString descriptionText;
    QString amountText;
    QString balanceText;
    // Formatted projection calculated from integer cents by the Binder. It is
    // informational only; the page must not use it to decide payment success.
    QString balanceAfterPaymentText;
    QString message;
    PaymentViewStatus status = PaymentViewStatus::Ready;
    bool canPay = false;
    bool canRecharge = true;
    bool canRecoverResult = false;
};

Q_DECLARE_METATYPE(PaymentPurpose)
Q_DECLARE_METATYPE(PaymentViewStatus)
Q_DECLARE_METATYPE(PaymentViewState)
