#include "modules/wallet/walletservice.h"

#include "modules/wallet/iwalletnetworkapi.h"

WalletService::WalletService(IWalletNetworkApi *network, QObject *parent)
    : IWalletService(parent), m_network(network)
{
    Q_ASSERT(m_network);
    connect(m_network, &IWalletNetworkApi::walletReady,
            this, &IWalletService::walletReady);
    connect(m_network, &IWalletNetworkApi::moneyOperationSucceeded,
            this, &IWalletService::moneyOperationSucceeded);
    connect(m_network, &IWalletNetworkApi::moneyOperationStatusReady,
            this, &IWalletService::moneyOperationStatusReady);
    connect(m_network, &IWalletNetworkApi::requestFailed,
            this, &IWalletService::requestFailed);
}

void WalletService::queryWallet(const RequestContext &context)
{
    if (!context.isValid() || context.isMutation()) {
        fail(context, QStringLiteral("wallet-invalid-query"),
             QStringLiteral("钱包查询参数无效。"));
        return;
    }
    if (!m_network->capabilities().walletSnapshotQuery) {
        fail(context, QStringLiteral("wallet-query-contract-required"),
             QStringLiteral("服务端尚未提供普通用户钱包查询合同。"));
        return;
    }
    m_network->queryWallet(context);
}

void WalletService::recharge(const RequestContext &context, qint64 amountCents)
{
    if (!context.isValid() || !context.isMutation() || amountCents <= 0) {
        fail(context, QStringLiteral("wallet-invalid-recharge"),
             QStringLiteral("充值请求参数无效。"));
        return;
    }
    // Stage E test integration: the current server exposes the one-shot
    // 113 -> 216 recharge contract, but not idempotent operation lookup.
    // The network adapter marks timeout/disconnect outcomes as unknown and
    // the UI locks retry, so do not silently retry this mutation.
    if (!m_network->capabilities().rechargeMessage) {
        fail(context, QStringLiteral("wallet-recharge-unsupported"),
             QStringLiteral("服务端尚未提供充值接口。"));
        return;
    }
    m_network->recharge(context, amountCents);
}

void WalletService::payOrder(const RequestContext &context,
                             const QString &orderId)
{
    if (!context.isValid() || !context.isMutation() || orderId.isEmpty()) {
        fail(context, QStringLiteral("wallet-invalid-payment"),
             QStringLiteral("订单支付请求参数无效。"));
        return;
    }
    if (!m_network->capabilities().canPayOrderSafely()) {
        fail(context, QStringLiteral("wallet-idempotency-contract-required"),
             QStringLiteral("服务端尚未提供幂等支付与结果查询能力。"));
        return;
    }
    m_network->payOrder(context, orderId);
}

void WalletService::queryOperationResult(const RequestContext &context,
                                         const QString &operationId)
{
    if (!context.isValid() || context.isMutation() || operationId.isEmpty()) {
        fail(context, QStringLiteral("wallet-invalid-operation-query"),
             QStringLiteral("资金操作结果查询参数无效。"));
        return;
    }
    if (!m_network->capabilities().operationResultQuery) {
        fail(context, QStringLiteral("wallet-operation-query-unsupported"),
             QStringLiteral("服务端尚未提供资金操作结果查询能力。"));
        return;
    }
    m_network->queryOperationResult(context, operationId);
}

void WalletService::cancel(const QString &requestId)
{
    m_network->cancel(requestId);
}

void WalletService::fail(const RequestContext &context, const QString &code,
                         const QString &message)
{
    ClientError error;
    error.requestId = context.requestId;
    error.operationId = context.operationId;
    error.code = code;
    error.displayMessage = message;
    emit requestFailed(error);
}
