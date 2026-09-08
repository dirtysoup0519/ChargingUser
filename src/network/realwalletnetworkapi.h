#pragma once

#include "common/connectionstate.h"
#include "modules/wallet/iwalletnetworkapi.h"

#include <QJsonObject>
#include <optional>

class BackendClient;
class QTimer;

/** v2.6 钱包适配器：100/200 查询余额与流水，113/216 单次充值。 */
class RealWalletNetworkApi final : public IWalletNetworkApi
{
    Q_OBJECT
public:
    explicit RealWalletNetworkApi(BackendClient *backend,
                                  QObject *parent = nullptr);

    WalletBackendCapabilities capabilities() const override;
    void setIdentity(const QString &username);
    void setRequestTimeoutMs(int timeoutMs);

public slots:
    void queryWallet(const RequestContext &context) override;
    void recharge(const RequestContext &context, qint64 amountCents) override;
    void payOrder(const RequestContext &context,
                  const QString &orderId) override;
    void queryOperationResult(const RequestContext &context,
                              const QString &operationId) override;
    void cancel(const QString &requestId) override;

private slots:
    void handleFrame(int msgType, const QJsonObject &payload);
    void handleConnectionStateChanged(ConnectionState state);
    void handleTimeout();

private:
    enum class PendingKind { WalletUser, WalletTransactions, Recharge, PayOrder };
    struct PendingRequest
    {
        PendingKind kind = PendingKind::WalletUser;
        RequestContext context;
        qint64 balanceCents = 0;
        QTimer *timer = nullptr;
    };

    bool begin(PendingKind kind, const RequestContext &context);
    bool sendTableQuery(const QString &table);
    void finishPending();
    void failPending(const QString &code, const QString &message,
                     bool retryable, bool resultUnknown = false);
    void emitFailure(const RequestContext &context, const QString &code,
                     const QString &message, bool retryable = false,
                     bool resultUnknown = false);
    static WalletTransaction parseTransaction(const QJsonObject &record);

    BackendClient *m_backend;
    QString m_username;
    int m_requestTimeoutMs = 10000;
    std::optional<PendingRequest> m_pending;
};
