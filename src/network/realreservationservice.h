#pragma once

#include "common/connectionstate.h"
#include "modules/reservation/ireservationservice.h"

#include <QJsonObject>
#include <optional>

class BackendClient;
class QTimer;

class RealReservationService final : public IReservationService
{
    Q_OBJECT
public:
    explicit RealReservationService(BackendClient *backend,
                                    QObject *parent = nullptr);
    void setIdentity(const QString &username);
    void setRequestTimeoutMs(int timeoutMs);

public slots:
    void reserve(const RequestContext &context,
                 const QString &stationId,
                 const QString &chargerId,
                 int durationSeconds) override;
    void cancel(const QString &requestId) override;

private slots:
    void handleFrame(int msgType, const QJsonObject &payload);
    void handleConnectionStateChanged(ConnectionState state);
    void handleTimeout();

private:
    struct PendingRequest
    {
        RequestContext context;
        QString stationId;
        QString chargerId;
        QTimer *timer = nullptr;
    };

    void finishPending();
    void failPending(const QString &code, const QString &message,
                     bool retryable, bool resultUnknown = false);
    void emitFailure(const RequestContext &context, const QString &code,
                     const QString &message, bool retryable = false,
                     bool resultUnknown = false);

    BackendClient *m_backend;
    QString m_username;
    int m_requestTimeoutMs = 10000;
    std::optional<PendingRequest> m_pending;
};
