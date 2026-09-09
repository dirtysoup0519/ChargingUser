#pragma once

#include "modules/reservation/ireservationservice.h"

#include <QHash>
#include <QSet>

class MockReservationService final : public IReservationService
{
    Q_OBJECT
public:
    enum class Outcome { Success, Failure, ResultUnknown };
    struct Behavior {
        Outcome outcome = Outcome::Success;
        int delayMs = 0;
        ClientError error;
    };

    explicit MockReservationService(QObject *parent = nullptr);
    void setReserveBehavior(const Behavior &behavior);
    void setCancellationBehavior(const Behavior &behavior);

public slots:
    void reserve(const RequestContext &context, const QString &stationId,
                 const QString &chargerId, int durationSeconds) override;
    void cancel(const QString &requestId) override;
    void cancelReservation(const RequestContext &context) override;
    void queryHistory(const RequestContext &context) override;

private:
    void fail(const RequestContext &context, const Behavior &behavior,
              const QString &fallbackMessage);
    Behavior m_reserveBehavior;
    Behavior m_cancellationBehavior;
    QHash<QString, ReservationResult> m_activeById;
    QSet<QString> m_cancelledRequests;
};
