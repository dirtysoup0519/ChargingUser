#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QVector>

enum class WalletTransactionType { Recharge, Payment, Refund, Deposit, Unknown };

struct WalletTransaction
{
    QString transactionId;
    WalletTransactionType type = WalletTransactionType::Unknown;
    qint64 amountCents = 0;
    qint64 balanceAfterCents = 0;
    QString orderId;
    QDateTime createdAtUtc;
};

struct WalletSnapshot
{
    QString accountId;
    qint64 balanceCents = 0;
    QVector<WalletTransaction> recentTransactions;
    QDateTime fetchedAtUtc;
    // 服务端流水查询失败（如不支持 walletTransaction 表）时的可见警告；
    // 余额仍为权威值，调用方不得把该警告当空流水静默吞掉。
    QString transactionsNotice;
};

enum class MoneyOperationType { Recharge, PayOrder };

struct MoneyOperationResult
{
    QString requestId;
    QString operationId;
    MoneyOperationType type = MoneyOperationType::Recharge;
    qint64 balanceCents = 0;
    QString orderId;
    QString transactionId;
};

enum class MoneyOperationState { Pending, Succeeded, Failed };

struct MoneyOperationStatus
{
    QString requestId;
    QString operationId;
    MoneyOperationState state = MoneyOperationState::Pending;
    QString failureCode;
    QString failureMessage;
};

Q_DECLARE_METATYPE(WalletTransactionType)
Q_DECLARE_METATYPE(WalletTransaction)
Q_DECLARE_METATYPE(WalletSnapshot)
Q_DECLARE_METATYPE(MoneyOperationType)
Q_DECLARE_METATYPE(MoneyOperationResult)
Q_DECLARE_METATYPE(MoneyOperationState)
Q_DECLARE_METATYPE(MoneyOperationStatus)
