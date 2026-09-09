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

void ReservationUiBinder::reserveRequested(const QString &stationId,
                                            const QString &chargerId,
                                            int durationSeconds)
{
    if (!m_requestId.isEmpty() || stationId.trimmed().isEmpty()
        || chargerId.trimmed().isEmpty() || durationSeconds <= 0) return;
    m_requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_operationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
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
    m_state.canReserve = true;
    publish();
}

void ReservationUiBinder::cancelReservationRequested(const QString &reservationId)
{
    if (!m_requestId.isEmpty() || reservationId.trimmed().isEmpty()) return;
    m_requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_operationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_state.status = ReservationConfirmationStatus::Submitting;
    m_state.canReserve = false;
    m_state.message = QStringLiteral("正在取消预约…");
    publish();
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
    m_state.status = ReservationConfirmationStatus::Ready;
    m_state.canReserve = false;
    m_state.message = result.reservationId.isEmpty()
                          ? QStringLiteral("预约已提交。")
                          : QStringLiteral("预约成功，编号：%1").arg(result.reservationId);
    publish();
}

void ReservationUiBinder::handleFailure(const ClientError &error)
{
    if (error.requestId != m_requestId) return;
    m_requestId.clear();
    m_operationId.clear();
    m_state.status = error.resultUnknown ? ReservationConfirmationStatus::ResultUnknown
                                         : ReservationConfirmationStatus::Error;
    m_state.canReserve = false;
    m_state.canRetry = !error.resultUnknown && error.retryable;
    m_state.message = error.displayMessage.isEmpty()
                          ? QStringLiteral("预约失败。") : error.displayMessage;
    publish();
}

void ReservationUiBinder::handleCancelled(const RequestContext &context,
                                          const ReservationCancellationResult &)
{
    if (context.requestId != m_requestId || context.operationId != m_operationId) return;
    m_requestId.clear();
    m_operationId.clear();
    m_state.status = ReservationConfirmationStatus::Ready;
    m_state.canReserve = false;
    m_state.message = QStringLiteral("预约已取消，押金已退回钱包。");
    publish();
}

void ReservationUiBinder::publish()
{
    emit stateChanged(m_state);
}
