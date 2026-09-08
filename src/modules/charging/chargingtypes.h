#pragma once

#include <QMetaType>
#include <QDateTime>
#include <QString>

#include <optional>

/** 进入启动操作前的只读权威快照；金额统一使用整数分。 */
struct ChargeConfirmationSnapshot
{
    QString stationId;
    QString chargerId;
    QString stationName;
    QString stationAddress;
    QString chargerCode;
    QString chargerType;
    std::optional<double> powerKw;
    std::optional<qint64> priceCentsPerKwh;
    std::optional<qint64> walletBalanceCents;
    bool canStart = false;
    /** 服务端具备 operationId 幂等和结果查询能力后才能置 true。 */
    bool startOperationSupported = false;
    bool canRecharge = false;
    QString disabledReason;
};

struct StartChargingResult
{
    QString requestId;
    QString operationId;
    QString orderId;
    QString stationId;
    QString chargerId;
    qint64 priceCentsPerKwhSnapshot = 0;
    QDateTime startedAtUtc;
};

enum class ChargingOperationState
{
    Pending,
    Succeeded,
    Failed
};

struct ChargingOperationStatus
{
    QString requestId;
    QString operationId;
    ChargingOperationState state = ChargingOperationState::Pending;
    std::optional<StartChargingResult> result;
    QString failureCode;
    QString failureMessage;
};

Q_DECLARE_METATYPE(ChargeConfirmationSnapshot)
Q_DECLARE_METATYPE(StartChargingResult)
Q_DECLARE_METATYPE(ChargingOperationState)
Q_DECLARE_METATYPE(ChargingOperationStatus)
