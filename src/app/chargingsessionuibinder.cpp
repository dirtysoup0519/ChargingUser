#include "app/chargingsessionuibinder.h"

#include "modules/order/iorderservice.h"

#include <QUuid>

ChargingSessionUiBinder::ChargingSessionUiBinder(IOrderService *service,
                                                 QObject *parent)
    : IChargingSessionUiBinder(parent), m_service(service)
{
    Q_ASSERT(m_service);
    qRegisterMetaType<ChargingSessionViewState>();
    connect(m_service, &IOrderService::orderDetailReady,
            this, &ChargingSessionUiBinder::handleOrderDetailReady);
    connect(m_service, &IOrderService::chargingStopped,
            this, &ChargingSessionUiBinder::handleChargingStopped);
    connect(m_service, &IOrderService::stopOperationStatusReady,
            this, &ChargingSessionUiBinder::handleStopOperationStatusReady);
    connect(m_service, &IOrderService::requestFailed,
            this, &ChargingSessionUiBinder::handleRequestFailed);
}

ChargingSessionViewState ChargingSessionUiBinder::currentState() const
{
    return m_state;
}

void ChargingSessionUiBinder::sessionRequested(const QString &orderId)
{
    const QString normalized = orderId.trimmed();
    if (normalized.isEmpty()) return;
    if (!m_requestId.isEmpty()) m_service->cancel(m_requestId);
    m_requestId.clear();
    m_operationId.clear();
    m_state = ChargingSessionViewState{};
    m_state.orderId = normalized;
    load();
}

void ChargingSessionUiBinder::refreshRequested()
{
    if (m_state.orderId.isEmpty() || !m_requestId.isEmpty()
        || m_state.status == ChargingSessionStatus::Stopping
        || m_state.status == ChargingSessionStatus::ResultUnknown)
        return;
    load();
}

void ChargingSessionUiBinder::stopChargingRequested()
{
    if (m_state.status != ChargingSessionStatus::Charging || !m_state.canStop
        || !m_requestId.isEmpty() || !m_operationId.isEmpty())
        return;
    RequestContext context;
    context.requestId = QStringLiteral("stop-charging-")
                        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    context.operationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_requestId = context.requestId;
    m_operationId = context.operationId;
    m_state.status = ChargingSessionStatus::Stopping;
    m_state.operationId = m_operationId;
    m_state.message = QStringLiteral("正在停止充电…");
    m_state.canStop = false;
    m_state.canRefresh = false;
    publish();
    m_service->stopCharging(context, m_state.orderId);
}

void ChargingSessionUiBinder::recoverStopResultRequested()
{
    if (m_state.status == ChargingSessionStatus::ResultUnknown
        && m_requestId.isEmpty() && !m_operationId.isEmpty())
        queryStopResult();
}

void ChargingSessionUiBinder::handleOrderDetailReady(
    const RequestContext &context, const ChargingOrder &order)
{
    if (context.requestId != m_requestId || context.isMutation()
        || order.orderId != m_state.orderId)
        return;
    m_requestId.clear();
    applyOrder(order);
    publish();
}

void ChargingSessionUiBinder::handleChargingStopped(
    const RequestContext &context, const StopChargingResult &result)
{
    if (context.requestId != m_requestId
        || context.operationId != m_operationId
        || result.operationId != m_operationId
        || result.order.orderId != m_state.orderId)
        return;
    m_requestId.clear();
    m_operationId.clear();
    applyOrder(result.order);
    m_state.status = ChargingSessionStatus::Ended;
    m_state.operationId.clear();
    m_state.message = QStringLiteral("充电已停止，结算结果以订单详情为准。");
    m_state.canStop = false;
    m_state.canRefresh = true;
    publish();
}

void ChargingSessionUiBinder::handleStopOperationStatusReady(
    const RequestContext &context, const StopOperationStatus &status)
{
    if (context.requestId != m_requestId
        || status.operationId != m_operationId)
        return;
    if (status.state == StopOperationState::Pending) {
        m_requestId.clear();
        m_state.status = ChargingSessionStatus::ResultUnknown;
        m_state.message = QStringLiteral("服务端仍在处理停止操作，请继续确认原操作结果。");
        m_state.canRecoverResult = true;
        publish();
        return;
    }
    if (status.state == StopOperationState::Succeeded && status.result) {
        if (status.result->operationId != m_operationId
            || status.result->order.orderId != m_state.orderId)
            return;
        m_requestId.clear();
        m_operationId.clear();
        applyOrder(status.result->order);
        m_state.status = ChargingSessionStatus::Ended;
        m_state.operationId.clear();
        m_state.message = QStringLiteral("充电已停止，结算结果以订单详情为准。");
        m_state.canStop = false;
        m_state.canRefresh = true;
        publish();
        return;
    }
    m_requestId.clear();
    m_operationId.clear();
    m_state.operationId.clear();
    m_state.status = ChargingSessionStatus::Error;
    m_state.message = status.failureMessage.isEmpty()
                          ? QStringLiteral("停止充电失败，请刷新订单后重试。")
                          : status.failureMessage;
    m_state.canRefresh = true;
    m_state.canRecoverResult = false;
    publish();
}

void ChargingSessionUiBinder::handleRequestFailed(const ClientError &error)
{
    if (error.requestId != m_requestId) return;
    m_requestId.clear();
    if (!m_operationId.isEmpty() && error.operationId == m_operationId
        && error.resultUnknown) {
        m_state.status = ChargingSessionStatus::ResultUnknown;
        m_state.message = QStringLiteral("停止结果未知，正在确认原操作结果…");
        m_state.canRecoverResult = true;
        m_state.canRefresh = false;
        publish();
        queryStopResult();
        return;
    }
    if (!m_operationId.isEmpty() && error.operationId == m_operationId) {
        m_operationId.clear();
        m_state.operationId.clear();
    }
    m_state.status = ChargingSessionStatus::Error;
    m_state.message = error.displayMessage.isEmpty()
                          ? QStringLiteral("订单状态加载失败。")
                          : error.displayMessage;
    m_state.canStop = false;
    m_state.canRefresh = error.retryable;
    m_state.canRecoverResult = false;
    publish();
}

void ChargingSessionUiBinder::load()
{
    if (m_state.orderId.isEmpty()) return;
    RequestContext context;
    context.requestId = QStringLiteral("order-detail-")
                        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_requestId = context.requestId;
    m_state.status = ChargingSessionStatus::Loading;
    m_state.message = QStringLiteral("正在加载充电订单…");
    m_state.canStop = false;
    m_state.canRefresh = false;
    publish();
    m_service->queryOrderDetail(context, m_state.orderId);
}

void ChargingSessionUiBinder::queryStopResult()
{
    if (m_operationId.isEmpty() || !m_requestId.isEmpty()) return;
    RequestContext context;
    context.requestId = QStringLiteral("query-stop-result-")
                        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_requestId = context.requestId;
    m_state.canRecoverResult = false;
    publish();
    m_service->queryStopResult(context, m_operationId);
}

void ChargingSessionUiBinder::applyOrder(const ChargingOrder &order)
{
    m_state.stationName = order.stationName;
    m_state.chargerCode = order.chargerCode;
    m_state.progressPercent = order.progressPercent.value_or(-1);
    m_state.currentPowerText = order.currentPowerKw
        ? QStringLiteral("%1 kW").arg(*order.currentPowerKw, 0, 'f', 1)
        : QStringLiteral("-- kW");
    m_state.ratedPowerText = order.ratedPowerKw
        ? QStringLiteral("%1 kW").arg(*order.ratedPowerKw, 0, 'f', 0)
        : QStringLiteral("-- kW");
    m_state.chargerTypeText = order.chargerType;
    m_state.energyText = QStringLiteral("%1 kWh").arg(order.energyKwh, 0, 'f', 2);
    m_state.amountText = QStringLiteral("¥%1").arg(order.amountCents / 100.0, 0, 'f', 2);
    m_state.startedAtText = order.startedAtUtc.isValid()
                                ? order.startedAtUtc.toLocalTime().toString(Qt::ISODate)
                                : QStringLiteral("--");
    if (order.startedAtUtc.isValid()) {
        const qint64 minutes = qMax<qint64>(
            0, order.startedAtUtc.secsTo(QDateTime::currentDateTimeUtc()) / 60);
        m_state.durationText = QStringLiteral("%1 分钟").arg(minutes);
    } else {
        m_state.durationText = QStringLiteral("--");
    }
    m_state.message.clear();
    m_state.canRecoverResult = false;
    m_state.canRefresh = true;
    if (order.status == OrderStatus::Charging) {
        m_state.status = ChargingSessionStatus::Charging;
        m_state.canStop = true;
    } else if (order.status == OrderStatus::PendingSettlement
               || order.status == OrderStatus::Settled
               || order.status == OrderStatus::Cancelled) {
        m_state.status = ChargingSessionStatus::Ended;
        m_state.canStop = false;
    } else {
        m_state.status = ChargingSessionStatus::Error;
        m_state.message = QStringLiteral("订单状态未知，请刷新后重试。");
        m_state.canStop = false;
    }
}

void ChargingSessionUiBinder::publish()
{
    emit sessionStateChanged(m_state);
}
