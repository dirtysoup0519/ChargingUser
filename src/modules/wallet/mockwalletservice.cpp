#include "modules/wallet/mockwalletservice.h"

#include <QTimer>
#include <QUuid>

MockWalletService::MockWalletService(QObject *parent) : IWalletService(parent) {}

void MockWalletService::setSnapshot(const WalletSnapshot &snapshot)
{
    m_snapshot = snapshot;
}

void MockWalletService::setOrderAmount(const QString &orderId, qint64 amountCents)
{
    if (!orderId.isEmpty() && amountCents >= 0)
        m_orderAmounts.insert(orderId, amountCents);
}

bool MockWalletService::debit(qint64 amountCents)
{
    if (amountCents < 0 || m_snapshot.balanceCents < amountCents) return false;
    m_snapshot.balanceCents -= amountCents;
    return true;
}

void MockWalletService::credit(qint64 amountCents)
{
    if (amountCents > 0) m_snapshot.balanceCents += amountCents;
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
    m_snapshot.balanceCents += amountCents;
    MoneyOperationResult result;
    result.requestId = context.requestId;
    result.operationId = context.operationId;
    result.type = MoneyOperationType::Recharge;
    result.balanceCents = m_snapshot.balanceCents;
    result.transactionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_operationResults.insert(context.operationId, result);
    QTimer::singleShot(0, this, [this, context, result] {
        emit moneyOperationSucceeded(context, result);
    });
}

void MockWalletService::payOrder(const RequestContext &context,
                                 const QString &orderId)
{
    if (!context.isValid() || !context.isMutation() || orderId.isEmpty()) {
        fail(context, QStringLiteral("wallet-invalid-payment"),
             QStringLiteral("支付请求缺少 operationId。"));
        return;
    }
    if (!m_orderAmounts.contains(orderId)) {
        fail(context, QStringLiteral("wallet-order-not-found"),
             QStringLiteral("未找到待支付订单。"));
        return;
    }
    const qint64 amount = m_orderAmounts.value(orderId);
    if (m_snapshot.balanceCents < amount) {
        fail(context, QStringLiteral("wallet-insufficient-balance"),
             QStringLiteral("钱包余额不足，请先充值。"));
        return;
    }
    m_snapshot.balanceCents -= amount;
    MoneyOperationResult result;
    result.requestId = context.requestId;
    result.operationId = context.operationId;
    result.type = MoneyOperationType::PayOrder;
    result.balanceCents = m_snapshot.balanceCents;
    result.orderId = orderId;
    result.transactionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_operationResults.insert(context.operationId, result);
    QTimer::singleShot(0, this, [this, context, result] {
        emit moneyOperationSucceeded(context, result);
    });
}

void MockWalletService::queryOperationResult(const RequestContext &context,
                                             const QString &operationId)
{
    if (!context.isValid() || context.isMutation() || operationId.isEmpty()) {
        fail(context, QStringLiteral("wallet-invalid-operation-query"),
             QStringLiteral("资金操作结果查询参数无效。"));
        return;
    }
    MoneyOperationStatus status;
    status.requestId = context.requestId;
    status.operationId = operationId;
    status.state = m_operationResults.contains(operationId)
        ? MoneyOperationState::Succeeded : MoneyOperationState::Pending;
    QTimer::singleShot(0, this, [this, context, status] {
        emit moneyOperationStatusReady(context, status);
    });
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
