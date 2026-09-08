#include "modules/charging/placeholderchargingnetworkapi.h"

PlaceholderChargingNetworkApi::PlaceholderChargingNetworkApi(QObject *parent)
    : IChargingNetworkApi(parent)
{
}

ChargingBackendCapabilities
PlaceholderChargingNetworkApi::capabilities() const
{
    // 占位实现不承诺任何服务端能力：全部 false，避免 UI 误判可发起变更操作。
    return {};
}

void PlaceholderChargingNetworkApi::loadConfirmation(const RequestContext &context,
                                                     const QString &stationId,
                                                     const QString &chargerId)
{
    Q_UNUSED(stationId);
    Q_UNUSED(chargerId);
    failPending(context);
}

void PlaceholderChargingNetworkApi::startCharging(const RequestContext &context,
                                                  const QString &stationId,
                                                  const QString &chargerId)
{
    Q_UNUSED(stationId);
    Q_UNUSED(chargerId);
    failPending(context);
}

void PlaceholderChargingNetworkApi::queryStartResult(const RequestContext &context,
                                                     const QString &operationId)
{
    Q_UNUSED(operationId);
    failPending(context);
}

void PlaceholderChargingNetworkApi::cancel(const QString &requestId)
{
    // 占位实现没有在途网络对象可中止；仅清理关联记录，
    // 迟到信号不可能出现（所有失败都是本地同步发出的）。
    if (m_lastRequestId == requestId) {
        m_lastRequestId.clear();
    }
}

void PlaceholderChargingNetworkApi::failPending(const RequestContext &context)
{
    m_lastRequestId = context.requestId;
    ClientError error;
    error.requestId = context.requestId;
    error.operationId = context.operationId;
    error.code = QStringLiteral("charging-adapter-pending");
    error.displayMessage = QStringLiteral("充电服务适配器尚未接入，功能开发中");
    // 可重试：阶段 F 接入后无需改页面即可恢复。
    error.retryable = true;
    error.resultUnknown = false;
    emit requestFailed(error);
}
