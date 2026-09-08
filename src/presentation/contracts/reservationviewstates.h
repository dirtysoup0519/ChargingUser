#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>

enum class ReservationConfirmationStatus { Idle, Loading, Ready, Submitting, Error, ResultUnknown };
enum class ReservationCancellationStatus { Idle, Submitting, Error, ResultUnknown };

struct ReservationConfirmationViewState
{
    QString stationId;
    QString chargerId;
    QString stationName;
    QString stationAddress;
    QString chargerCode;
    QString chargerTypeText;
    QString powerText;
    QString depositText;
    QString durationText;
    QString depositPolicyText;
    int durationSeconds = 0;
    ReservationConfirmationStatus status = ReservationConfirmationStatus::Idle;
    QString message;
    bool canReserve = false;
    bool canRetry = false;
    QString disabledReason;
};

struct ActiveReservationView
{
    QString reservationId;
    QString stationId;
    QString chargerId;
    QDateTime expiresAtUtc;
    QString remainingText;
    bool canCancel = false;
    bool canRetryCancel = false;
    ReservationCancellationStatus cancellationStatus = ReservationCancellationStatus::Idle;
    QString cancellationMessage;
    QString cancelDisabledReason;
};

Q_DECLARE_METATYPE(ReservationConfirmationStatus)
Q_DECLARE_METATYPE(ReservationConfirmationViewState)
Q_DECLARE_METATYPE(ReservationCancellationStatus)
Q_DECLARE_METATYPE(ActiveReservationView)
