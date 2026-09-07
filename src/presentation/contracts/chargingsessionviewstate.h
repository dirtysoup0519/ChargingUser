#pragma once

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

// 仅定义 UI 可消费的业务状态，不规定任何控件、布局或视觉样式。
struct ChargingSessionViewState
{
    QString orderId;
    ChargingSessionStatus status = ChargingSessionStatus::Idle;
    QString stationName;
    QString chargerCode;
    QString energyText;
    QString amountText;
    QString startedAtText;
    QString operationId;
    QString message;
    bool canStop = false;
    bool canRefresh = false;
    bool canRecoverResult = false;
};

Q_DECLARE_METATYPE(ChargingSessionStatus)
Q_DECLARE_METATYPE(ChargingSessionViewState)
