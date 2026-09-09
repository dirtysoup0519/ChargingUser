#pragma once

#include "presentation/contracts/chargingviewstates.h"
#include "modules/charging/chargingtypes.h"

#include <QObject>

class IChargingUiBinder : public QObject
{
    Q_OBJECT
public:
    explicit IChargingUiBinder(QObject *parent = nullptr) : QObject(parent) {}
    ~IChargingUiBinder() override = default;
    virtual ChargeConfirmationViewState currentState() const = 0;

public slots:
    virtual void chargeConfirmationRequested(const QString &stationId,
                                             const QString &chargerId) = 0;
    virtual void confirmationRefreshRequested() = 0;
    virtual void backRequested() = 0;
    virtual void startChargingRequested(const QString &stationId,
                                        const QString &chargerId) = 0;
    virtual void recoverStartResultRequested() = 0;
    virtual void rechargeRequested() = 0;

signals:
    void confirmationStateChanged(const ChargeConfirmationViewState &state);
    void confirmationPageRequested();
    void stationDetailPageRequested();
    void rechargePageRequested();
    void chargingSessionRequested(const StartChargingResult &result);
};
