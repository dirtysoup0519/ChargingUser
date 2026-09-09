#include "app/charginguibinder.h"

#include "modules/charging/ichargingservice.h"

#include <QUuid>

namespace {
QString moneyText(const std::optional<qint64> &cents)
{
    return cents ? QStringLiteral("¥%1").arg(*cents / 100.0, 0, 'f', 2)
                 : QStringLiteral("待接入");
}
}

ChargingUiBinder::ChargingUiBinder(IChargingService *service, QObject *parent)
    : IChargingUiBinder(parent), m_service(service)
{
    Q_ASSERT(m_service);
    qRegisterMetaType<ChargeConfirmationViewState>();
    connect(m_service, &IChargingService::confirmationReady,
            this, &ChargingUiBinder::handleConfirmationReady);
    connect(m_service, &IChargingService::chargingStarted,
            this, &ChargingUiBinder::handleChargingStarted);
    connect(m_service, &IChargingService::startOperationStatusReady,
            this, &ChargingUiBinder::handleStartOperationStatusReady);
    connect(m_service, &IChargingService::requestFailed,
            this, &ChargingUiBinder::handleRequestFailed);
}

ChargeConfirmationViewState ChargingUiBinder::currentState() const
{
    return m_state;
}

void ChargingUiBinder::setReservationActive(bool active)
{
    m_reservationActive = active;
    if (!active)
        m_reservationChargerCode.clear();
}

void ChargingUiBinder::setReservationChargerCode(const QString &chargerCode)
{
    m_reservationChargerCode = chargerCode.trimmed();
}

void ChargingUiBinder::chargeConfirmationRequested(const QString &stationId,
                                                    const QString &chargerId)
{
    if (m_reservationActive) {
        m_state.status = ChargeConfirmationStatus::Error;
        m_state.canStart = false;
        m_state.message = QStringLiteral("当前账号已有预约，请前往预约充电桩。");
        m_state.disabledReason = m_state.message;
        publish();
        return;
    }
    if (stationId.trimmed().isEmpty() || chargerId.trimmed().isEmpty())
        return;
    if (!m_requestId.isEmpty()) {
        m_service->cancel(m_requestId);
        m_requestId.clear();
    }
    m_state = ChargeConfirmationViewState{};
    m_state.stationId = stationId;
    m_state.chargerId = chargerId;
    emit confirmationPageRequested();
    load();
}

void ChargingUiBinder::chargeConfirmationByChargerCodeRequested(
    const QString &chargerCode)
{
    if (m_reservationActive) {
        m_state.status = ChargeConfirmationStatus::Error;
        m_state.canStart = false;
        m_state.message = QStringLiteral("当前账号已有预约，请前往预约充电桩。");
        m_state.disabledReason = m_state.message;
        publish();
        return;
    }
    const QString normalized = chargerCode.trimmed();
    if (normalized.isEmpty()) return;
    if (!m_requestId.isEmpty()) {
        m_service->cancel(m_requestId);
        m_requestId.clear();
    }
    m_state = ChargeConfirmationViewState{};
    m_state.chargerId = normalized;
    emit confirmationPageRequested();
    load();
}

void ChargingUiBinder::confirmationRefreshRequested()
{
    if (m_state.status != ChargeConfirmationStatus::Loading
        && m_state.status != ChargeConfirmationStatus::Submitting
        && m_state.status != ChargeConfirmationStatus::ResultUnknown)
        load();
}

void ChargingUiBinder::backRequested()
{
    // 变更请求提交中或结果未知时不能通过返回取消语义掩盖服务端结果。
    if (m_state.status == ChargeConfirmationStatus::Submitting
        || m_state.status == ChargeConfirmationStatus::ResultUnknown)
        return;
    if (!m_requestId.isEmpty()) {
        m_service->cancel(m_requestId);
        m_requestId.clear();
    }
    emit stationDetailPageRequested();
}

void ChargingUiBinder::startChargingRequested(const QString &stationId,
                                              const QString &chargerId)
{
    if (m_state.status != ChargeConfirmationStatus::Ready || !m_state.canStart
        || stationId != m_state.stationId || chargerId != m_state.chargerId)
        return;
    RequestContext context;
    context.requestId = QStringLiteral("start-charging-")
                        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    context.operationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_requestId = context.requestId;
    m_operationId = context.operationId;
    m_state.status = ChargeConfirmationStatus::Submitting;
    m_state.operationId = m_operationId;
    m_state.message = QStringLiteral("正在启动充电…");
    m_state.canStart = false;
    m_state.canRetry = false;
    publish();
    m_service->startCharging(context, stationId, chargerId);
}

void ChargingUiBinder::recoverStartResultRequested()
{
    if (m_state.status == ChargeConfirmationStatus::ResultUnknown
        && !m_operationId.isEmpty() && m_requestId.isEmpty())
        queryStartResult();
}

void ChargingUiBinder::rechargeRequested()
{
    if (m_state.canRecharge)
        emit rechargePageRequested();
}

void ChargingUiBinder::handleConfirmationReady(
    const RequestContext &context, const ChargeConfirmationSnapshot &snapshot)
{
    if (context.requestId != m_requestId
        || (!m_state.stationId.isEmpty() && snapshot.stationId != m_state.stationId)
        || snapshot.chargerId.compare(m_state.chargerId, Qt::CaseInsensitive) != 0)
        return;
    m_requestId.clear();
    m_state.stationId = snapshot.stationId;
    m_state.status = ChargeConfirmationStatus::Ready;
    m_state.stationName = snapshot.stationName;
    m_state.stationAddress = snapshot.stationAddress;
    m_state.chargerCode = snapshot.chargerCode;
    m_state.chargerTypeText = snapshot.chargerType;
    m_state.powerText = snapshot.powerKw
                            ? QStringLiteral("%1 kW").arg(*snapshot.powerKw, 0, 'f', 1)
                            : QStringLiteral("功率未知");
    m_state.chargerStatusText = snapshot.canStart
                                    ? QStringLiteral("空闲")
                                    : QStringLiteral("不可启动");
    m_state.energyPriceText = moneyText(snapshot.priceCentsPerKwh)
                              + QStringLiteral("/kWh");
    m_state.walletBalanceText = moneyText(snapshot.walletBalanceCents);
    m_state.canStart = snapshot.canStart && snapshot.startOperationSupported
                       && !snapshot.hasActiveOrder;
    m_state.canRetry = false;
    m_state.canRecharge = snapshot.canRecharge;
    if (snapshot.hasActiveOrder) {
        m_state.disabledReason = snapshot.activeOrderId.isEmpty()
                                     ? QStringLiteral("当前账号已有进行中的订单。")
                                     : QStringLiteral("当前账号已有订单 %1。")
                                           .arg(snapshot.activeOrderId);
    } else {
        m_state.disabledReason = snapshot.canStart
                                     && !snapshot.startOperationSupported
                                 ? QStringLiteral("订单启动接口待接入。")
                                 : snapshot.disabledReason;
    }
    m_state.message.clear();
    publish();
}

void ChargingUiBinder::handleRequestFailed(const ClientError &error)
{
    if (error.requestId != m_requestId)
        return;
    m_requestId.clear();
    // 启动结果恢复（ORDERQRY）明确查不到充电订单时，不再按“结果未知”循环重试。
    // 若账号存在预约，提示用户前往预约桩或按协议 109 取消预约。
    if (m_reservationActive && error.code == QLatin1String("order-not-found")) {
        m_operationId.clear();
        m_state.operationId.clear();
        const QString charger = m_reservationChargerCode.isEmpty()
                                    ? m_state.chargerId
                                    : m_reservationChargerCode;
        m_state.status = ChargeConfirmationStatus::Error;
        m_state.message = charger.isEmpty()
                              ? QStringLiteral("您有进行中的预约，请前往预约桩充电或取消预约（109）")
                              : QStringLiteral("您有进行中的预约（电桩 %1），请前往预约桩充电或取消预约（109）")
                                    .arg(charger);
        m_state.disabledReason = m_state.message;
        m_state.canRetry = false;
        m_state.canStart = false;
        publish();
        return;
    }
    if (!m_operationId.isEmpty() && error.operationId == m_operationId
        && error.resultUnknown) {
        m_state.status = ChargeConfirmationStatus::ResultUnknown;
        m_state.operationId = m_operationId;
        m_state.message = QStringLiteral("启动结果暂时未知，正在按原操作确认…");
        m_state.canRetry = false;
        m_state.canStart = false;
        publish();
        queryStartResult();
        return;
    }
    if (!m_operationId.isEmpty() && error.operationId == m_operationId) {
        m_operationId.clear();
        m_state.operationId.clear();
    }
    m_state.status = ChargeConfirmationStatus::Error;
    m_state.message = error.displayMessage.isEmpty()
                          ? QStringLiteral("充电确认信息加载失败。")
                          : error.displayMessage;
    m_state.canRetry = error.retryable;
    m_state.canStart = false;
    publish();
}

void ChargingUiBinder::handleChargingStarted(const RequestContext &context,
                                             const StartChargingResult &result)
{
    if (context.requestId != m_requestId || context.operationId != m_operationId
        || result.operationId != m_operationId)
        return;
    m_requestId.clear();
    m_operationId.clear();
    emit chargingSessionRequested(result);
}

void ChargingUiBinder::handleStartOperationStatusReady(
    const RequestContext &context, const ChargingOperationStatus &status)
{
    if (context.requestId != m_requestId || status.operationId != m_operationId)
        return;
    m_requestId.clear();
    if (status.state == ChargingOperationState::Pending) {
        m_state.status = ChargeConfirmationStatus::ResultUnknown;
        m_state.message = QStringLiteral("服务端仍在处理启动操作，请稍后继续确认。");
        publish();
        return;
    }
    if (status.state == ChargingOperationState::Succeeded && status.result) {
        const StartChargingResult result = *status.result;
        m_operationId.clear();
        emit chargingSessionRequested(result);
        return;
    }
    m_operationId.clear();
    m_state.operationId.clear();
    m_state.status = ChargeConfirmationStatus::Error;
    m_state.message = status.failureMessage.isEmpty()
                          ? QStringLiteral("启动充电失败，请重新核对电桩状态。")
                          : status.failureMessage;
    m_state.canRetry = true;
    publish();
}

void ChargingUiBinder::load()
{
    // 扫码入口只有 chargerCode，所属站点由 119/229 权威响应补齐。
    if (m_state.chargerId.isEmpty())
        return;
    if (!m_requestId.isEmpty())
        m_service->cancel(m_requestId);
    RequestContext context;
    context.requestId = QStringLiteral("charge-confirmation-")
                        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_requestId = context.requestId;
    m_state.status = ChargeConfirmationStatus::Loading;
    m_state.message = QStringLiteral("正在核对充电桩状态…");
    m_state.canRetry = false;
    m_state.canStart = false;
    publish();
    m_service->loadConfirmation(context, m_state.stationId, m_state.chargerId);
}

void ChargingUiBinder::queryStartResult()
{
    if (m_operationId.isEmpty() || !m_requestId.isEmpty())
        return;
    RequestContext context;
    context.requestId = QStringLiteral("query-start-result-")
                        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_requestId = context.requestId;
    m_service->queryStartResult(context, m_operationId);
}

void ChargingUiBinder::publish()
{
    emit confirmationStateChanged(m_state);
}
