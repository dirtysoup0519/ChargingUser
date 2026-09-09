#include "reservationuibinder.h"

#include "modules/reservation/ireservationservice.h"

#include <QUuid>

ReservationUiBinder::ReservationUiBinder(IReservationService *service,
                                         QObject *parent)
    : QObject(parent), m_service(service)
{
    Q_ASSERT(m_service);
    connect(m_service, &IReservationService::reservationCreated,
            this, &ReservationUiBinder::handleCreated);
    connect(m_service, &IReservationService::requestFailed,
            this, &ReservationUiBinder::handleFailure);
    connect(m_service, &IReservationService::reservationCancelled,
            this, &ReservationUiBinder::handleCancelled);
}

ReservationConfirmationViewState ReservationUiBinder::currentState() const
{
    return m_state;
}

std::optional<ActiveReservationView> ReservationUiBinder::currentActiveReservation() const
{
    return m_activeReservation;
}

void ReservationUiBinder::restoreActiveReservation(
    const std::optional<ActiveReservationView> &reservation)
{
    m_activeReservation = reservation;
    emit activeReservationChanged(m_activeReservation);
}

void ReservationUiBinder::expireReservationIfNeeded()
{
    if (!m_activeReservation || !m_activeReservation->expiresAtUtc.isValid()
        || m_activeReservation->expiresAtUtc > QDateTime::currentDateTimeUtc())
        return;
    m_activeReservation.reset();
    emit activeReservationChanged(m_activeReservation);
}

void ReservationUiBinder::consumeActiveReservation()
{
    if (!m_activeReservation)
        return;
    m_activeReservation.reset();
    emit activeReservationChanged(m_activeReservation);
}

void ReservationUiBinder::setConfirmationState(
    const ReservationConfirmationViewState &state)
{
    if (!m_requestId.isEmpty()) return;
    m_state = state;
    m_state.message.clear();
    m_state.canRetry = false;
    publish();
}

void ReservationUiBinder::reserveRequested(const QString &stationId,
                                            const QString &chargerId,
                                            int durationSeconds)
{
    if (!m_requestId.isEmpty() || stationId.trimmed().isEmpty()
        || chargerId.trimmed().isEmpty() || durationSeconds <= 0) return;
    m_requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_operationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_cancelling = false;
    m_state.stationId = stationId;
    m_state.chargerId = chargerId;
    m_state.durationSeconds = durationSeconds;
    m_state.status = ReservationConfirmationStatus::Submitting;
    m_state.canReserve = false;
    m_state.message = QStringLiteral("正在提交预约…");
    publish();
    m_service->reserve({m_requestId, m_operationId}, stationId, chargerId,
                       durationSeconds);
}

void ReservationUiBinder::refreshRequested()
{
    if (m_state.status == ReservationConfirmationStatus::ResultUnknown) return;
    m_state.status = ReservationConfirmationStatus::Ready;
    m_state.canReserve = !m_activeReservation.has_value();
    publish();
}

void ReservationUiBinder::cancelReservationRequested(const QString &reservationId)
{
    if (!m_requestId.isEmpty() || reservationId.trimmed().isEmpty()) return;
    m_requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_operationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_cancelling = true;
    m_state.status = ReservationConfirmationStatus::Submitting;
    m_state.canReserve = false;
    m_state.message = QStringLiteral("正在取消预约…");
    publish();
    if (m_activeReservation) {
        m_activeReservation->cancellationStatus = ReservationCancellationStatus::Submitting;
        m_activeReservation->cancellationMessage = m_state.message;
        m_activeReservation->canCancel = false;
        m_activeReservation->canRetryCancel = false;
        emit activeReservationChanged(m_activeReservation);
    }
    m_service->cancelReservation({m_requestId, m_operationId});
}

void ReservationUiBinder::cancelReservationRetryRequested(const QString &reservationId)
{
    cancelReservationRequested(reservationId);
}

void ReservationUiBinder::handleCreated(const RequestContext &context,
                                        const ReservationResult &result)
{
    if (context.requestId != m_requestId || context.operationId != m_operationId) return;
    m_requestId.clear();
    m_operationId.clear();
    m_cancelling = false;
    m_state.status = ReservationConfirmationStatus::Ready;
    m_state.canReserve = false;
    m_state.canRetry = false;
    if (m_state.chargerCode.isEmpty())
        m_state.chargerCode = result.chargerCode;
    m_state.message = result.reservationId.isEmpty()
                          ? QStringLiteral("预约已提交。")
                          : QStringLiteral("预约成功，编号：%1").arg(result.reservationId);
    publish();
    ActiveReservationView active;
    active.reservationId = result.reservationId;
    active.stationId = result.stationId.isEmpty() ? m_state.stationId : result.stationId;
    active.chargerId = result.chargerId.isEmpty() ? m_state.chargerId : result.chargerId;
    active.expiresAtUtc = result.expiresAtUtc;
    active.remainingText = result.expiresAtUtc.isValid()
        ? QStringLiteral("预约已生效，截止 %1")
              .arg(result.expiresAtUtc.toLocalTime().toString(QStringLiteral("MM-dd HH:mm")))
        : QStringLiteral("预约已生效");
    active.canCancel = true;
    m_activeReservation = active;
    emit activeReservationChanged(m_activeReservation);
}

void ReservationUiBinder::handleFailure(const ClientError &error)
{
    if (error.requestId != m_requestId) return;
    const bool cancellationFailed = m_cancelling;
    m_requestId.clear();
    m_operationId.clear();
    m_cancelling = false;
    m_state.status = error.resultUnknown ? ReservationConfirmationStatus::ResultUnknown
                                         : ReservationConfirmationStatus::Error;
    m_state.canReserve = false;
    m_state.canRetry = !error.resultUnknown && error.retryable;
    m_state.message = error.displayMessage.isEmpty()
                          ? QStringLiteral("预约失败。") : error.displayMessage;
    publish();
    if (cancellationFailed && m_activeReservation) {
        m_activeReservation->cancellationStatus = error.resultUnknown
            ? ReservationCancellationStatus::ResultUnknown
            : ReservationCancellationStatus::Error;
        m_activeReservation->cancellationMessage = m_state.message;
        m_activeReservation->canCancel = false;
        m_activeReservation->canRetryCancel = !error.resultUnknown && error.retryable;
        emit activeReservationChanged(m_activeReservation);
    }
}

void ReservationUiBinder::handleCancelled(const RequestContext &context,
                                          const ReservationCancellationResult &)
{
    if (context.requestId != m_requestId || context.operationId != m_operationId) return;
    m_requestId.clear();
    m_operationId.clear();
    m_cancelling = false;
    m_state.status = ReservationConfirmationStatus::Ready;
    m_state.canReserve = false;
    m_state.message = QStringLiteral("预约已取消，押金已退回钱包。");
    publish();
    m_activeReservation.reset();
    emit activeReservationChanged(m_activeReservation);
}

void ReservationUiBinder::publish()
{
    emit stateChanged(m_state);
}
