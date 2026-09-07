#pragma once

#include "common/clienterror.h"
#include "common/requestcontext.h"
#include "modules/wallet/walletbackendcapabilities.h"
#include "modules/wallet/wallettypes.h"

#include <QObject>

class IWalletNetworkApi : public QObject
{
    Q_OBJECT
public:
    explicit IWalletNetworkApi(QObject *parent = nullptr) : QObject(parent) {}
    ~IWalletNetworkApi() override = default;
    virtual WalletBackendCapabilities capabilities() const = 0;

public slots:
    virtual void queryWallet(const RequestContext &context) = 0;
    virtual void recharge(const RequestContext &context, qint64 amountCents) = 0;
    virtual void payOrder(const RequestContext &context,
                          const QString &orderId) = 0;
    virtual void queryOperationResult(const RequestContext &context,
                                      const QString &operationId) = 0;
    virtual void cancel(const QString &requestId) = 0;

signals:
    void walletReady(const RequestContext &context,
                     const WalletSnapshot &snapshot);
    void moneyOperationSucceeded(const RequestContext &context,
                                 const MoneyOperationResult &result);
    void moneyOperationStatusReady(const RequestContext &context,
                                   const MoneyOperationStatus &status);
    void requestFailed(const ClientError &error);
};
