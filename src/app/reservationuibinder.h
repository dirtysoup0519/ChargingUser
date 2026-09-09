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

    // Set the authoritative snapshot used by the confirmation page before a
    // reserve request starts. Result updates must preserve these display
    // fields because the service response only contains reservation data.
    void setConfirmationState(const ReservationConfirmationViewState &state);

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
