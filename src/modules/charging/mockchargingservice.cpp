#include "modules/charging/mockchargingservice.h"

#include <QTimer>

MockChargingService::MockChargingService(QObject *parent)
    : IChargingService(parent)
{
}

void MockChargingService::setSnapshots(
    const QVector<ChargeConfirmationSnapshot> &snapshots)
{
    m_snapshots.clear();
    for (const ChargeConfirmationSnapshot &snapshot : snapshots) {
        if (!snapshot.stationId.isEmpty() && !snapshot.chargerId.isEmpty())
            m_snapshots.insert(key(snapshot.stationId, snapshot.chargerId), snapshot);
    }
}

void MockChargingService::setChargerAvailable(const QString &stationId,
                                              const QString &chargerId,
                                              bool available)
{
    const QString snapshotKey = key(stationId, chargerId);
    if (!m_snapshots.contains(snapshotKey)) return;
    ChargeConfirmationSnapshot snapshot = m_snapshots.value(snapshotKey);
    snapshot.canStart = available;
    m_snapshots.insert(snapshotKey, snapshot);
}

void MockChargingService::loadConfirmation(const RequestContext &context,
                                           const QString &stationId,
                                           const QString &chargerId)
{
    if (!context.isValid() || context.isMutation() || stationId.trimmed().isEmpty()
        || chargerId.trimmed().isEmpty()) {
        ClientError error;
        error.requestId = context.requestId;
        error.code = QStringLiteral("charging-invalid-confirmation-query");
        error.displayMessage = QStringLiteral("充电确认参数无效。");
        emit requestFailed(error);
        return;
    }
    const QString requestId = context.requestId;
    const QString snapshotKey = key(stationId, chargerId);
    QTimer::singleShot(0, this, [this, context, requestId, snapshotKey] {
        if (m_cancelled.remove(requestId))
            return;
        if (!m_snapshots.contains(snapshotKey)) {
            ClientError error;
            error.requestId = requestId;
            error.code = QStringLiteral("charging-confirmation-not-found");
            error.displayMessage = QStringLiteral("充电桩状态已变化，请返回后刷新。");
            error.retryable = true;
            emit requestFailed(error);
            return;
        }
        emit confirmationReady(context, m_snapshots.value(snapshotKey));
    });
}

void MockChargingService::cancel(const QString &requestId)
{
    if (!requestId.isEmpty())
        m_cancelled.insert(requestId);
}

void MockChargingService::startCharging(const RequestContext &context,
                                        const QString &stationId,
                                        const QString &chargerId)
{
    const QString snapshotKey = key(stationId, chargerId);
    QTimer::singleShot(0, this, [this, context, stationId, chargerId, snapshotKey] {
        if (m_cancelled.remove(context.requestId)) return;
        if (!context.isValid() || !context.isMutation()
            || !m_snapshots.contains(snapshotKey) || !m_snapshots.value(snapshotKey).canStart) {
            ClientError error;
            error.requestId = context.requestId;
            error.operationId = context.operationId;
            error.code = QStringLiteral("charging-start-rejected");
            error.displayMessage = QStringLiteral("当前充电桩不可启动。");
            emit requestFailed(error);
            return;
        }
        const ChargeConfirmationSnapshot snapshot = m_snapshots.value(snapshotKey);
        setChargerAvailable(stationId, chargerId, false);
        StartChargingResult result;
        result.requestId = context.requestId;
        result.operationId = context.operationId;
        result.orderId = QStringLiteral("demo-charge-%1").arg(context.operationId);
        result.stationId = stationId;
        result.chargerId = chargerId;
        result.priceCentsPerKwhSnapshot = snapshot.priceCentsPerKwh.value_or(0);
        result.startedAtUtc = QDateTime::currentDateTimeUtc();
        emit chargingStarted(context, result);
    });
}

void MockChargingService::queryStartResult(const RequestContext &context,
                                           const QString &operationId)
{
    ChargingOperationStatus status;
    status.requestId = context.requestId;
    status.operationId = operationId;
    status.state = ChargingOperationState::Pending;
    QTimer::singleShot(0, this, [this, context, status] {
        emit startOperationStatusReady(context, status);
    });
}

QString MockChargingService::key(const QString &stationId,
                                 const QString &chargerId)
{
    return stationId + QChar(0x1f) + chargerId;
}
