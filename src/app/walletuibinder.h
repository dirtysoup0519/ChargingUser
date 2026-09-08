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

    IWalletService *m_service;
    QString m_accountId;
    QString m_requestId;
    QString m_operationId;
    WalletViewState m_state;
};
