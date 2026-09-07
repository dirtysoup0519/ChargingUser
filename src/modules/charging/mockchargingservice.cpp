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
    Q_UNUSED(stationId)
    Q_UNUSED(chargerId)
    ClientError error;
    error.requestId = context.requestId;
    error.operationId = context.operationId;
    error.code = QStringLiteral("charging-start-not-configured");
    error.displayMessage = QStringLiteral("订单启动接口尚未接入。");
    emit requestFailed(error);
}

void MockChargingService::queryStartResult(const RequestContext &context,
                                           const QString &operationId)
{
    ClientError error;
    error.requestId = context.requestId;
    error.operationId = operationId;
    error.code = QStringLiteral("charging-operation-query-not-configured");
    error.displayMessage = QStringLiteral("启动结果查询接口尚未接入。");
    emit requestFailed(error);
}

QString MockChargingService::key(const QString &stationId,
                                 const QString &chargerId)
{
    return stationId + QChar(0x1f) + chargerId;
}
