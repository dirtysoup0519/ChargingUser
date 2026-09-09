#pragma once

#include "common/clienterror.h"
#include "common/requestcontext.h"
#include "modules/reservation/reservationtypes.h"
#include "presentation/contracts/reservationviewstates.h"

#include <QObject>

class IReservationService;

class ReservationUiBinder final : public QObject
{
    Q_OBJECT
public:
    explicit ReservationUiBinder(IReservationService *service,
                                 QObject *parent = nullptr);
    ReservationConfirmationViewState currentState() const;

public slots:
    void reserveRequested(const QString &stationId, const QString &chargerId,
                          int durationSeconds);
    void refreshRequested();
    void cancelReservationRequested(const QString &reservationId);
    void cancelReservationRetryRequested(const QString &reservationId);

private slots:
    void handleCreated(const RequestContext &context,
                       const ReservationResult &result);
    void handleFailure(const ClientError &error);
    void handleCancelled(const RequestContext &context,
                         const ReservationCancellationResult &result);

signals:
    void stateChanged(const ReservationConfirmationViewState &state);

private:
    void publish();
    IReservationService *m_service;
    ReservationConfirmationViewState m_state;
    QString m_requestId;
    QString m_operationId;
};
