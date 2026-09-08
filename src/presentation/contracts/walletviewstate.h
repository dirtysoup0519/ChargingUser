#pragma once

#include <QMetaType>
#include <QString>

enum class WalletPageStatus
{
    Idle,
    Loading,
    Ready,
    Submitting,
    Succeeded,
    Error,
    ResultUnknown
};

struct WalletViewState
{
    WalletPageStatus status = WalletPageStatus::Idle;
    QString balanceText;
    QString message;
    bool canSubmit = false;
};

Q_DECLARE_METATYPE(WalletPageStatus)
Q_DECLARE_METATYPE(WalletViewState)
