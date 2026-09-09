#pragma once

#include "app/ichargingsessionuibinder.h"
#include "common/clienterror.h"
#include "common/requestcontext.h"
#include "modules/order/ordertypes.h"

class IOrderService;
struct ChargingProgressNotice;

class ChargingSessionUiBinder final : public IChargingSessionUiBinder
{
    Q_OBJECT
public:
    explicit ChargingSessionUiBinder(IOrderService *service,
                                     QObject *parent = nullptr);
    ChargingSessionViewState currentState() const override;
    void showOrder(const ChargingOrder &order);
    void applyProgress(const ChargingProgressNotice &notice);

public slots:
    void sessionRequested(const QString &orderId) override;
    void refreshRequested() override;
    void stopChargingRequested() override;
    void recoverStopResultRequested() override;

private slots:
    void handleOrderDetailReady(const RequestContext &context,
                                const ChargingOrder &order);
    void handleChargingStopped(const RequestContext &context,
                               const StopChargingResult &result);
    void handleStopOperationStatusReady(const RequestContext &context,
                                        const StopOperationStatus &status);
    void handleRequestFailed(const ClientError &error);

private:
    void load();
    void queryStopResult();
    void applyOrder(const ChargingOrder &order);
    void publish();

    IOrderService *m_service;
    ChargingSessionViewState m_state;
    QString m_requestId;
    QString m_operationId;
};
