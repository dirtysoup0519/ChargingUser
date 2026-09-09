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
    void setReservationActive(bool active);
    void setReservationChargerCode(const QString &chargerCode);

public slots:
    void chargeConfirmationRequested(const QString &stationId,
                                     const QString &chargerId) override;
    /** 扫码入口：仅携带 chargerCode，由服务端解析所属站点。 */
    void chargeConfirmationByChargerCodeRequested(const QString &chargerCode);
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
    bool m_reservationActive = false;
    QString m_reservationChargerCode;
};
