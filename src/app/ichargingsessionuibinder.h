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
    virtual ChargingSessionCollectionViewState currentSessionsState() const
    {
        return {};
    }

public slots:
    virtual void sessionRequested(const QString &orderId) = 0;
    virtual void refreshRequested() = 0;
    virtual void stopChargingRequested() = 0;
    virtual void recoverStopResultRequested() = 0;
    /** Loads every active order. Existing single-session binders may no-op. */
    virtual void activeSessionsRequested() {}
    /** Clears the in-flight session state (used on back/logout). */
    virtual void clearSession() {}
    /** Selects which order subsequent refresh/stop intents address. */
    virtual void activeSessionSelected(const QString &orderId)
    {
        sessionRequested(orderId);
    }

signals:
    void sessionStateChanged(const ChargingSessionViewState &state);
    void activeSessionsStateChanged(const ChargingSessionCollectionViewState &state);
};
