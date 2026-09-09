#pragma once

#include "presentation/contracts/reservationviewstates.h"
#include <QWidget>

namespace Ui { class ReservationConfirmationWindow; }

class ReservationConfirmationWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit ReservationConfirmationWindow(QWidget *parent = nullptr);
    ~ReservationConfirmationWindow() override;
    void render(const ReservationConfirmationViewState &state);

signals:
    void backRequested();
    void reservationRefreshRequested();
    void reserveRequested(const QString &stationId, const QString &chargerId,
                          int durationSeconds);

private:
    Ui::ReservationConfirmationWindow *ui;
    ReservationConfirmationViewState m_state;
};
