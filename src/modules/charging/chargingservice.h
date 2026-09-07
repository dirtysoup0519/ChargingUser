#pragma once

#include "modules/charging/ichargingservice.h"

class IChargingNetworkApi;

/** 依据显式协议能力控制真实操作，不允许仅因 108/208 存在就启用启动。 */
class ChargingService final : public IChargingService
{
    Q_OBJECT
public:
    explicit ChargingService(IChargingNetworkApi *network,
                             QObject *parent = nullptr);

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
    void fail(const RequestContext &context, const QString &code,
              const QString &message);
    IChargingNetworkApi *m_network;
};
