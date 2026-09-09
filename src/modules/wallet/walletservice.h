#pragma once

#include "modules/wallet/iwalletservice.h"

class IWalletNetworkApi;

class WalletService final : public IWalletService
{
    Q_OBJECT
public:
    explicit WalletService(IWalletNetworkApi *network, QObject *parent = nullptr);

public slots:
    void queryWallet(const RequestContext &context) override;
    void recharge(const RequestContext &context, qint64 amountCents) override;
    void payOrder(const RequestContext &context,
                  const QString &orderId) override;
    void queryOperationResult(const RequestContext &context,
                              const QString &operationId) override;
    void cancel(const QString &requestId) override;

private:
    void fail(const RequestContext &context, const QString &code,
              const QString &message);
    IWalletNetworkApi *m_network;
};
