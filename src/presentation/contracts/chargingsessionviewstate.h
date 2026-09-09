#pragma once

#include <QList>
#include <QMetaType>
#include <QString>

enum class ChargingSessionStatus
{
    Idle,
    Loading,
    Charging,
    Stopping,
    ResultUnknown,
    Ended,
    Error
};

// Describes the selected charging order. Numeric business values are formatted
// by the Binder so the page never parses units or calculates authoritative data.
struct ChargingSessionViewState
{
    QString orderId;
    ChargingSessionStatus status = ChargingSessionStatus::Idle;
    QString stationName;
    QString chargerCode;
    QString chargerTypeText;
    QString ratedPowerText;
    QString currentPowerText;
    QString energyText;
    QString durationText;
    QString amountText;
    QString startedAtText;
    int progressPercent = -1;
    QString operationId;
    QString message;
    bool canStop = false;
    bool canRefresh = false;
    bool canRecoverResult = false;
};

struct ChargingSessionSummaryView
{
    QString orderId;
    QString stationName;
    QString chargerCode;
    QString chargerTypeText;
    QString ratedPowerText;
    QString currentPowerText;
    QString durationText;
    QString statusText;
    ChargingSessionStatus status = ChargingSessionStatus::Idle;
};

struct ChargingSessionCollectionViewState
{
    QList<ChargingSessionSummaryView> sessions;
    QString selectedOrderId;
    QString message;
    bool loading = false;
    bool canRefresh = false;
};

Q_DECLARE_METATYPE(ChargingSessionStatus)
Q_DECLARE_METATYPE(ChargingSessionViewState)
Q_DECLARE_METATYPE(ChargingSessionSummaryView)
Q_DECLARE_METATYPE(ChargingSessionCollectionViewState)
