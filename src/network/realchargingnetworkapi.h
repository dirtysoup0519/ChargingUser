#pragma once

#include "common/connectionstate.h"
#include "modules/charging/ichargingnetworkapi.h"

#include <QJsonObject>
#include <initializer_list>
#include <optional>

class BackendClient;
class QTimer;

    /** 阶段 F 真实确认适配器：119/229 查询站点电桩，100/200 补查余额和活动订单。 */
class RealChargingNetworkApi final : public IChargingNetworkApi
{
    Q_OBJECT
public:
    explicit RealChargingNetworkApi(BackendClient *backend,
                                    QObject *parent = nullptr);

    ChargingBackendCapabilities capabilities() const override;
    void setIdentity(const QString &username);
    void setUnsafeTestOperationsEnabled(bool enabled);
    void setRequestTimeoutMs(int timeoutMs);

public slots:
    void loadConfirmation(const RequestContext &context,
                          const QString &stationId,
                          const QString &chargerId) override;
    void startCharging(const RequestContext &context,
                       const QString &stationId,
                       const QString &chargerId) override;
    void queryStartResult(const RequestContext &context,
                          const QString &operationId) override;
    void cancel(const QString &requestId) override;

private slots:
    void handleFrame(int msgType, const QJsonObject &payload);
    void handleConnectionStateChanged(ConnectionState state);
    void handleTimeout();

private:
    enum class PendingKind {
        ConfirmationStation,
        ConfirmationUser,
        ConfirmationOrders,
        Start
    };
    struct PendingRequest
    {
        PendingKind kind = PendingKind::ConfirmationStation;
        RequestContext context;
        QString stationId;
        QString chargerId;
        QJsonObject station;
        QJsonObject charger;
        qint64 balanceCents = 0;
        QString activeOrderId;
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
    void publishConfirmation(const PendingRequest &pending);
    static QString stringField(const QJsonObject &object,
                               std::initializer_list<const char *> keys);
    static bool boolField(const QJsonObject &object, const char *key);
    static std::optional<qint64> centsField(const QJsonObject &object,
                                            const char *centsKey,
                                            const char *yuanKey);
    static std::optional<double> numberField(const QJsonObject &object,
                                             const char *key);

    BackendClient *m_backend;
    QString m_username;
    bool m_unsafeTestOperations = false;
    int m_requestTimeoutMs = 10000;
    std::optional<PendingRequest> m_pending;
};
