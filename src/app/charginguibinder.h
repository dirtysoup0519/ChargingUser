#pragma once

#include "app/icharginguibinder.h"
#include "common/clienterror.h"
#include "common/requestcontext.h"
#include "modules/charging/chargingtypes.h"

class IChargingService;

class ChargingUiBinder final : public IChargingUiBinder
{
    Q_OBJECT
public:
    explicit ChargingUiBinder(IChargingService *service,
                              QObject *parent = nullptr);
    ChargeConfirmationViewState currentState() const override;

public slots:
    void chargeConfirmationRequested(const QString &stationId,
                                     const QString &chargerId) override;
    void confirmationRefreshRequested() override;
    void backRequested() override;
    void startChargingRequested(const QString &stationId,
                                const QString &chargerId) override;
    void recoverStartResultRequested() override;
    void rechargeRequested() override;

private slots:
    void handleConfirmationReady(const RequestContext &context,
                                 const ChargeConfirmationSnapshot &snapshot);
    void handleChargingStarted(const RequestContext &context,
                               const StartChargingResult &result);
    void handleStartOperationStatusReady(
        const RequestContext &context,
        const ChargingOperationStatus &status);
    void handleRequestFailed(const ClientError &error);

private:
    void load();
    void queryStartResult();
    void publish();
    IChargingService *m_service;
    ChargeConfirmationViewState m_state;
    QString m_requestId;
    QString m_operationId;
};
