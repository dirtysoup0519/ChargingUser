#pragma once

#include "modules/charging/ichargingservice.h"

#include <QHash>
#include <QSet>

/** Demo/测试专用确认快照服务。 */
class MockChargingService final : public IChargingService
{
    Q_OBJECT
public:
    explicit MockChargingService(QObject *parent = nullptr);
    void setSnapshots(const QVector<ChargeConfirmationSnapshot> &snapshots);
    void setChargerAvailable(const QString &stationId, const QString &chargerId,
                             bool available);

public slots:
    void loadConfirmation(const RequestContext &context,
                          const QString &stationId,
                          const QString &chargerId) override;
    void startCharging(const RequestContext &context,
                       const QString &stationId,
                       const QString &chargerId) override;
    void queryStartResult(const RequestContext &context,
                          const QString &operationId) override;
    void cancel(const QString &requestId) override;

private:
    static QString key(const QString &stationId, const QString &chargerId);
    QHash<QString, ChargeConfirmationSnapshot> m_snapshots;
    QSet<QString> m_cancelled;
};
