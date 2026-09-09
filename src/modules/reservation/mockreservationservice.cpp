#include "mockreservationservice.h"

#include <QDateTime>
#include <QTimer>
#include <QUuid>

MockReservationService::MockReservationService(QObject *parent)
    : IReservationService(parent)
{
}

void MockReservationService::setReserveBehavior(const Behavior &behavior)
{
    m_reserveBehavior = behavior;
}

void MockReservationService::setCancellationBehavior(const Behavior &behavior)
{
    m_cancellationBehavior = behavior;
}

void MockReservationService::reserve(const RequestContext &context,
                                     const QString &stationId,
                                     const QString &chargerId,
                                     int durationSeconds)
{
    const Behavior behavior = m_reserveBehavior;
    QTimer::singleShot(qMax(0, behavior.delayMs), this,
                       [this, context, stationId, chargerId, durationSeconds, behavior] {
        if (m_cancelledRequests.remove(context.requestId)) return;
        if (behavior.outcome != Outcome::Success) {
            fail(context, behavior, QStringLiteral("预约创建失败。"));
            return;
        }
        ReservationResult result;
        result.reservationId = QStringLiteral("demo-reservation-%1")
                                   .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        result.stationId = stationId;
        result.chargerId = chargerId;
        result.reservedAtUtc = QDateTime::currentDateTimeUtc();
        result.expiresAtUtc = result.reservedAtUtc.addSecs(durationSeconds);
        m_activeById.insert(result.reservationId, result);
        emit reservationCreated(context, result);
    });
}

void MockReservationService::cancel(const QString &requestId)
{
    m_cancelledRequests.insert(requestId);
}

void MockReservationService::cancelReservation(const RequestContext &context)
{
    const Behavior behavior = m_cancellationBehavior;
    QTimer::singleShot(qMax(0, behavior.delayMs), this, [this, context, behavior] {
        if (m_cancelledRequests.remove(context.requestId)) return;
        if (behavior.outcome != Outcome::Success) {
            fail(context, behavior, QStringLiteral("取消预约失败。"));
            return;
        }
        ReservationCancellationResult result;
        if (!m_activeById.isEmpty()) {
            result.reservationId = m_activeById.constBegin().key();
            result.chargerCode = m_activeById.constBegin()->chargerId;
            m_activeById.erase(m_activeById.begin());
        }
        emit reservationCancelled(context, result);
    });
}

void MockReservationService::queryHistory(const RequestContext &context)
{
    QVector<ReservationHistoryItem> history;
    for (const ReservationResult &reservation : m_activeById) {
        ReservationHistoryItem item;
        item.reservationId = reservation.reservationId;
        item.chargerCode = reservation.chargerId;
        item.status = QStringLiteral("RESERVED");
        item.createdAtUtc = reservation.reservedAtUtc;
        item.reserveAtUtc = reservation.expiresAtUtc;
        history.append(item);
    }
    QTimer::singleShot(0, this, [this, context, history] {
        emit reservationHistoryReady(context, history);
    });
}

void MockReservationService::fail(const RequestContext &context,
                                  const Behavior &behavior,
                                  const QString &fallbackMessage)
{
    ClientError error = behavior.error;
    error.requestId = context.requestId;
    error.operationId = context.operationId;
    error.resultUnknown = behavior.outcome == Outcome::ResultUnknown;
    if (error.displayMessage.isEmpty()) error.displayMessage = fallbackMessage;
    emit requestFailed(error);
}
