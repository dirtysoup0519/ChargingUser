#pragma once

#include "common/clienterror.h"
#include "common/requestcontext.h"
#include "modules/charging/chargingtypes.h"

#include <QObject>

/** 充电流程领域边界。本阶段只实现只读确认快照，不创建订单。 */
class IChargingService : public QObject
{
    Q_OBJECT
public:
    explicit IChargingService(QObject *parent = nullptr) : QObject(parent) {}
    ~IChargingService() override = default;

public slots:
    virtual void loadConfirmation(const RequestContext &context,
                                  const QString &stationId,
                                  const QString &chargerId) = 0;
    /** 变更请求：context.operationId 必须非空，服务端必须按其幂等。 */
    virtual void startCharging(const RequestContext &context,
                               const QString &stationId,
                               const QString &chargerId) = 0;
    /** 只查询原 operationId 的权威结果，不得创建新的启动操作。 */
    virtual void queryStartResult(const RequestContext &context,
                                  const QString &operationId) = 0;
    virtual void cancel(const QString &requestId) = 0;

signals:
    void confirmationReady(const RequestContext &context,
                           const ChargeConfirmationSnapshot &snapshot);
    void chargingStarted(const RequestContext &context,
                         const StartChargingResult &result);
    void startOperationStatusReady(const RequestContext &context,
                                   const ChargingOperationStatus &status);
    void requestFailed(const ClientError &error);
};
