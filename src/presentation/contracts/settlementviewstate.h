#pragma once

#include <QMetaType>
#include <QString>

enum class SettlementPageStatus
{
    Idle,
    Ready,
    Submitting,
    Settled,
    Error,
    ResultUnknown
};

struct SettlementViewState
{
    SettlementPageStatus status = SettlementPageStatus::Idle;
    QString orderId;
    QString amountText;
    QString balanceText;
    QString deadlineText;
    QString message;
    bool canPay = false;
};

Q_DECLARE_METATYPE(SettlementPageStatus)
Q_DECLARE_METATYPE(SettlementViewState)
