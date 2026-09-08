#pragma once

#include "presentation/contracts/chargingsessionviewstate.h"

#include <QObject>

class IChargingSessionUiBinder : public QObject
{
    Q_OBJECT
public:
    explicit IChargingSessionUiBinder(QObject *parent = nullptr) : QObject(parent) {}
    ~IChargingSessionUiBinder() override = default;
    virtual ChargingSessionViewState currentState() const = 0;

public slots:
    virtual void sessionRequested(const QString &orderId) = 0;
    virtual void refreshRequested() = 0;
    virtual void stopChargingRequested() = 0;
    virtual void recoverStopResultRequested() = 0;

signals:
    void sessionStateChanged(const ChargingSessionViewState &state);
};
