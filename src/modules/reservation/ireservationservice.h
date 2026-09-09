#pragma once

#include "common/clienterror.h"
#include "common/requestcontext.h"
#include "modules/reservation/reservationtypes.h"

#include <QObject>
#include <QVector>

class IReservationService : public QObject
{
    Q_OBJECT
public:
    explicit IReservationService(QObject *parent = nullptr) : QObject(parent) {}
    ~IReservationService() override = default;

public slots:
    virtual void reserve(const RequestContext &context,
                         const QString &stationId,
                         const QString &chargerId,
                         int durationSeconds) = 0;
    virtual void cancel(const QString &requestId) = 0;
    virtual void cancelReservation(const RequestContext &context) = 0;
    virtual void queryHistory(const RequestContext &context) = 0;

signals:
    void reservationCreated(const RequestContext &context,
                             const ReservationResult &result);
    void requestFailed(const ClientError &error);
    void reservationCancelled(const RequestContext &context,
                              const ReservationCancellationResult &result);
    void reservationHistoryReady(const RequestContext &context,
                                 const QVector<ReservationHistoryItem> &items);
};
