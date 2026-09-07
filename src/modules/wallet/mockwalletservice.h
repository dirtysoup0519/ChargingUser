#pragma once

#include "modules/wallet/iwalletservice.h"

#include <QSet>

class MockWalletService final : public IWalletService
{
    Q_OBJECT
public:
    explicit MockWalletService(QObject *parent = nullptr);
    void setSnapshot(const WalletSnapshot &snapshot);

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
    WalletSnapshot m_snapshot;
    QSet<QString> m_cancelled;
};
