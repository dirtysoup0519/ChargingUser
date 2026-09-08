#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QVector>

#include <optional>

enum class OrderStatus { Charging, PendingSettlement, Settled, Cancelled, Unknown };

struct ChargingOrder
{
    QString orderId; // v2.5 orderNo 的领域名；须确认服务端保证不可变且唯一
    QString stationId;
    QString chargerId;
    QString stationName;
    QString chargerCode;
    OrderStatus status = OrderStatus::Unknown;
    qint64 priceCentsPerKwhSnapshot = 0;
    double energyKwh = 0.0;
    qint64 amountCents = 0;
    QDateTime startedAtUtc;
    std::optional<QDateTime> endedAtUtc;
    std::optional<QDateTime> paymentDeadlineUtc;
};

struct StopChargingResult
{
    QString requestId;
    QString operationId;
    ChargingOrder order;
};

enum class StopOperationState { Pending, Succeeded, Failed };

struct StopOperationStatus
{
    QString requestId;
    QString operationId;
    StopOperationState state = StopOperationState::Pending;
    std::optional<StopChargingResult> result;
    QString failureCode;
    QString failureMessage;
};

Q_DECLARE_METATYPE(OrderStatus)
Q_DECLARE_METATYPE(ChargingOrder)
Q_DECLARE_METATYPE(StopChargingResult)
Q_DECLARE_METATYPE(StopOperationState)
Q_DECLARE_METATYPE(StopOperationStatus)
Q_DECLARE_METATYPE(std::optional<ChargingOrder>)
