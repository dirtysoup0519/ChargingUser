#include "modules/wallet/mockwalletservice.h"

#include <QTimer>

MockWalletService::MockWalletService(QObject *parent) : IWalletService(parent) {}

void MockWalletService::setSnapshot(const WalletSnapshot &snapshot)
{
    m_snapshot = snapshot;
}

void MockWalletService::queryWallet(const RequestContext &context)
{
    if (!context.isValid() || context.isMutation()) {
        fail(context, QStringLiteral("wallet-invalid-query"),
             QStringLiteral("钱包查询参数无效。"));
        return;
    }
    QTimer::singleShot(0, this, [this, context] {
        if (m_cancelled.remove(context.requestId)) return;
        emit walletReady(context, m_snapshot);
    });
}

void MockWalletService::recharge(const RequestContext &context,
                                 qint64 amountCents)
{
    if (!context.isValid() || !context.isMutation() || amountCents <= 0) {
        fail(context, QStringLiteral("wallet-invalid-recharge"),
             QStringLiteral("充值请求缺少 operationId。"));
        return;
    }
    fail(context, QStringLiteral("wallet-recharge-not-configured"),
         QStringLiteral("充值接口尚未接入。"));
}

void MockWalletService::payOrder(const RequestContext &context,
                                 const QString &orderId)
{
    if (!context.isValid() || !context.isMutation() || orderId.isEmpty()) {
        fail(context, QStringLiteral("wallet-invalid-payment"),
             QStringLiteral("支付请求缺少 operationId。"));
        return;
    }
    fail(context, QStringLiteral("wallet-payment-not-configured"),
         QStringLiteral("订单支付接口尚未接入。"));
}

void MockWalletService::queryOperationResult(const RequestContext &context,
                                             const QString &operationId)
{
    if (!context.isValid() || context.isMutation() || operationId.isEmpty()) {
        fail(context, QStringLiteral("wallet-invalid-operation-query"),
             QStringLiteral("资金操作结果查询参数无效。"));
        return;
    }
    fail(context, QStringLiteral("wallet-operation-query-not-configured"),
         QStringLiteral("资金操作结果查询接口尚未接入。"));
}

void MockWalletService::cancel(const QString &requestId)
{
    if (!requestId.isEmpty()) m_cancelled.insert(requestId);
}

void MockWalletService::fail(const RequestContext &context,
                             const QString &code, const QString &message)
{
    ClientError error;
    error.requestId = context.requestId;
    error.operationId = context.operationId;
    error.code = code;
    error.displayMessage = message;
    emit requestFailed(error);
}
