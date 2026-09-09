#include "modules/charging/chargingservice.h"

#include "modules/charging/ichargingnetworkapi.h"

ChargingService::ChargingService(IChargingNetworkApi *network, QObject *parent)
    : IChargingService(parent), m_network(network)
{
    Q_ASSERT(m_network);
    connect(m_network, &IChargingNetworkApi::confirmationReady,
            this, &IChargingService::confirmationReady);
    connect(m_network, &IChargingNetworkApi::chargingStarted,
            this, &IChargingService::chargingStarted);
    connect(m_network, &IChargingNetworkApi::startOperationStatusReady,
            this, &IChargingService::startOperationStatusReady);
    connect(m_network, &IChargingNetworkApi::requestFailed,
            this, &IChargingService::requestFailed);
}

void ChargingService::loadConfirmation(const RequestContext &context,
                                       const QString &stationId,
                                       const QString &chargerId)
{
    // 扫码查询允许 stationId 为空；所属站点必须由 119/229 权威应答解析。
    if (!context.isValid() || context.isMutation() || chargerId.isEmpty()) {
        fail(context, QStringLiteral("charging-invalid-confirmation-query"),
             QStringLiteral("充电确认查询参数无效。"));
        return;
    }
    if (!m_network->capabilities().canLoadAuthoritativeConfirmation()) {
        fail(context, QStringLiteral("charging-stable-id-contract-required"),
             QStringLiteral("服务端尚未提供稳定站点与电桩 ID。"));
        return;
    }
    m_network->loadConfirmation(context, stationId, chargerId);
}

void ChargingService::startCharging(const RequestContext &context,
                                    const QString &stationId,
                                    const QString &chargerId)
{
    if (!context.isValid() || !context.isMutation() || stationId.isEmpty()
        || chargerId.isEmpty()) {
        fail(context, QStringLiteral("charging-invalid-start-request"),
             QStringLiteral("启动充电请求参数无效。"));
        return;
    }
    if (!m_network->capabilities().canStartChargingSafely()) {
        fail(context, QStringLiteral("charging-idempotency-contract-required"),
             QStringLiteral("服务端尚未提供幂等启动与操作结果查询能力。"));
        return;
    }
    m_network->startCharging(context, stationId, chargerId);
}

void ChargingService::queryStartResult(const RequestContext &context,
                                       const QString &operationId)
{
    if (!context.isValid() || context.isMutation() || operationId.isEmpty()) {
        fail(context, QStringLiteral("charging-invalid-operation-query"),
             QStringLiteral("启动结果查询参数无效。"));
        return;
    }
    if (!m_network->capabilities().operationResultQuery) {
        fail(context, QStringLiteral("charging-operation-query-unsupported"),
             QStringLiteral("服务端尚未提供操作结果查询能力。"));
        return;
    }
    m_network->queryStartResult(context, operationId);
}

void ChargingService::cancel(const QString &requestId)
{
    m_network->cancel(requestId);
}

void ChargingService::fail(const RequestContext &context, const QString &code,
                           const QString &message)
{
    ClientError error;
    error.requestId = context.requestId;
    error.operationId = context.operationId;
    error.code = code;
    error.displayMessage = message;
    emit requestFailed(error);
}
