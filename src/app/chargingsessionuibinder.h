#pragma once

#include "app/ichargingsessionuibinder.h"
#include "common/clienterror.h"
#include "common/requestcontext.h"
#include "modules/order/ordertypes.h"

class IOrderService;
struct ChargingProgressNotice;
class QTimer;

class ChargingSessionUiBinder final : public IChargingSessionUiBinder
{
    Q_OBJECT
public:
    explicit ChargingSessionUiBinder(IOrderService *service,
                                     QObject *parent = nullptr);
    ChargingSessionViewState currentState() const override;
    ChargingSessionCollectionViewState currentSessionsState() const override;
    void showOrder(const ChargingOrder &order);
    void applyProgress(const ChargingProgressNotice &notice);
    // Clears a completed/abandoned session before leaving the charging page.
    void clearSession();

public slots:
    void sessionRequested(const QString &orderId) override;
    void refreshRequested() override;
    void stopChargingRequested() override;
    void recoverStopResultRequested() override;
    void activeSessionsRequested() override;
    void activeSessionSelected(const QString &orderId) override;

private slots:
    void handleOrderDetailReady(const RequestContext &context,
                                const ChargingOrder &order);
    void handleChargingStopped(const RequestContext &context,
                               const StopChargingResult &result);
    void handleStopOperationStatusReady(const RequestContext &context,
                                        const StopOperationStatus &status);
    void handleRequestFailed(const ClientError &error);
    void handleActiveOrdersReady(const RequestContext &context,
                                 const QVector<ChargingOrder> &orders);

private:
    void load();
    void queryStopResult();
    void applyOrder(const ChargingOrder &order);
    void updateRemainingText();
    void publish();

    IOrderService *m_service;
    ChargingSessionViewState m_state;
    QString m_requestId;
    QString m_operationId;
    QString m_activeRequestId;
    ChargingSessionCollectionViewState m_sessionsState;
    QTimer *m_refreshTimer = nullptr;
};
