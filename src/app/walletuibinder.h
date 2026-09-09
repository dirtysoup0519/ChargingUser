#pragma once

#include "presentation/contracts/walletviewstate.h"

#include <QObject>
#include <QVector>

class ClientError;
class IWalletService;
class MoneyOperationResult;
class RequestContext;
class WalletSnapshot;
struct WalletTransaction;

class WalletUiBinder final : public QObject
{
    Q_OBJECT
public:
    explicit WalletUiBinder(IWalletService *service, QObject *parent = nullptr);

    WalletViewState currentState() const;
    void setAccountId(const QString &accountId);

public slots:
    void activate();
    void rechargeRequested(const QString &amountText);

signals:
    void stateChanged(const WalletViewState &state);
    void profileRefreshRequested();

private slots:
    void handleWalletReady(const RequestContext &context,
                           const WalletSnapshot &snapshot);
    void handleOperationSucceeded(const RequestContext &context,
                                  const MoneyOperationResult &result);
    void handleFailure(const ClientError &error);

private:
    void publish();
    static QString moneyText(qint64 cents);
    static QString transactionText(const QVector<WalletTransaction> &transactions);
    static bool parseAmountCents(const QString &text, qint64 *amountCents);
    static QVector<WalletTransaction> mergeTransactions(
        const QVector<WalletTransaction> &serverRows,
        const QVector<WalletTransaction> &localRecharges);
    void loadRecharges();
    void saveRecharges();

    IWalletService *m_service;
    QString m_accountId;
    QString m_requestId;
    QString m_operationId;
    // 由 216 充值回执确认的充值账单（本地持久化）；
    // 服务端流水表不可查询时，充值账单依然可见。
    QVector<WalletTransaction> m_recentRecharges;
    qint64 m_submittedAmountCents = 0;
    WalletViewState m_state;
};
