#pragma once

#include "common/clienterror.h"
#include "common/requestcontext.h"
#include "modules/charging/chargingbackendcapabilities.h"
#include "modules/charging/chargingtypes.h"

#include <QObject>

/** 协议适配边界；具体实现负责消息码、JSON、超时及应答关联。 */
class IChargingNetworkApi : public QObject
{
    Q_OBJECT
public:
    explicit IChargingNetworkApi(QObject *parent = nullptr) : QObject(parent) {}
    ~IChargingNetworkApi() override = default;
    virtual ChargingBackendCapabilities capabilities() const = 0;

public slots:
    virtual void loadConfirmation(const RequestContext &context,
                                  const QString &stationId,
                                  const QString &chargerId) = 0;
    virtual void startCharging(const RequestContext &context,
                               const QString &stationId,
                               const QString &chargerId) = 0;
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
