#pragma once

#include "presentation/contracts/settlementviewstate.h"

#include <QObject>

#include "modules/order/ordertypes.h"

class ClientError;
class IWalletService;
class MoneyOperationResult;
class RequestContext;

class SettlementUiBinder final : public QObject
{
    Q_OBJECT
public:
    explicit SettlementUiBinder(IWalletService *walletService,
                                QObject *parent = nullptr);

    SettlementViewState currentState() const;

public slots:
    void showOrder(const ChargingOrder &order);
    void payRequested();
    void refreshRequested();

signals:
    void stateChanged(const SettlementViewState &state);
    void orderRefreshRequested();
    void walletRefreshRequested();

private slots:
    void handleOperationSucceeded(const RequestContext &context,
                                  const MoneyOperationResult &result);
    void handleFailure(const ClientError &error);

private:
    void publish();
    static QString moneyText(qint64 cents);

    IWalletService *m_walletService;
    SettlementViewState m_state;
    QString m_requestId;
    QString m_operationId;
};
