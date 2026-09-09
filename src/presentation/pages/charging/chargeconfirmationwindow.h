#pragma once

#include "presentation/contracts/chargingviewstates.h"

#include <QWidget>

namespace Ui { class ChargeConfirmationWindow; }

class ChargeConfirmationWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit ChargeConfirmationWindow(QWidget *parent = nullptr);
    ~ChargeConfirmationWindow() override;
    void render(const ChargeConfirmationViewState &state);

signals:
    void backRequested();
    void confirmationRefreshRequested();
    void startChargingRequested(const QString &stationId, const QString &chargerId);
    void rechargeRequested();

private:
    Ui::ChargeConfirmationWindow *ui;
    ChargeConfirmationViewState m_state;
};
