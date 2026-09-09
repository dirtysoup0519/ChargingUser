#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>

struct ReservationResult
{
    QString reservationId;
    QString stationId;
    QString chargerId;
    QString chargerCode;
    QDateTime reservedAtUtc;
    QDateTime expiresAtUtc;
    qint64 balanceCents = 0;
};

struct ReservationCancellationResult
{
    QString reservationId;
    QString chargerCode;
    qint64 refundCents = 0;
    qint64 balanceCents = 0;
};

struct ReservationHistoryItem
{
    QString reservationId;
    QString stationName;
    QString chargerCode;
    qint64 depositCents = 0;
    QString status;
    QDateTime createdAtUtc;
    QDateTime reserveAtUtc;
};

Q_DECLARE_METATYPE(ReservationResult)
Q_DECLARE_METATYPE(ReservationCancellationResult)
Q_DECLARE_METATYPE(ReservationHistoryItem)
