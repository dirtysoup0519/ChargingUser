#pragma once

#include "modules/charging/ichargingnetworkapi.h"

/**
 * 阶段 B 占位实现：让充电确认/充电会话页面在真实入口可到达，
 * 但不产生任何伪数据（规划 §3：TMP 不得作为真实请求的静默回退）。
 *
 * 所有请求一律以显式错误结束（charger-adapter-pending），页面呈现
 * 可重试的失败态；阶段 F 的 RealChargingNetworkApi（108/208/109/209）
 * 就绪后整体替换本类，删除本文件。
 */
class PlaceholderChargingNetworkApi final : public IChargingNetworkApi
{
    Q_OBJECT

public:
    explicit PlaceholderChargingNetworkApi(QObject *parent = nullptr);

    ChargingBackendCapabilities capabilities() const override;

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
    void failPending(const RequestContext &context);

    /** 记录最近一次请求上下文，cancel 语义需要知道被取消的对象。 */
    QString m_lastRequestId;
};
