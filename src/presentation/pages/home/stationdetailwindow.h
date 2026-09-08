#pragma once

#include "presentation/contracts/mapviewstates.h"

#include <QWidget>

namespace Ui { class StationDetailWindow; }

class StationDetailWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit StationDetailWindow(QWidget *parent = nullptr);
    ~StationDetailWindow() override;

    void render(const StationDetailViewState &state);

signals:
    void backRequested();
    void stationRefreshRequested();
    void routePreviewRequested(TravelMode mode);
    void chargerSelected(const QString &chargerId);
    void chargeConfirmationRequested(const QString &stationId,
                                     const QString &chargerId);
    void reservationConfirmationRequested(const QString &stationId,
                                           const QString &chargerId);
    void reservationExpiredRefreshRequested();
    void cancelReservationRequested(const QString &reservationId);
    void cancelReservationRetryRequested(const QString &reservationId);
    void activeReservationRequested(const QString &reservationId,
                                    const QString &stationId,
                                    const QString &chargerId);
    // 集成期兼容旧 Demo；新 Binder 接通后不再连接此信号。
    void navigationRequested();
    // 充电合同冻结前保留但不由页面按钮发出。
    void chargeRequested();

private:
    void rebuildChargers(const QVector<ChargerListItemView> &chargers);
    void selectCharger(const ChargerListItemView &charger);
    void updateReservationCountdown();

    Ui::StationDetailWindow *ui;
    class InteractiveMapWidget *m_map = nullptr;
    StationDetailViewState m_state;
    class QFrame *m_reservationBanner = nullptr;
    class QLabel *m_reservationTitle = nullptr;
    class QLabel *m_reservationCountdown = nullptr;
    class QPushButton *m_cancelReservationButton = nullptr;
    class QLabel *m_cancellationStateLabel = nullptr;
    class QLabel *m_reservationRestrictionLabel = nullptr;
    class QTimer *m_reservationTimer = nullptr;
    bool m_expiryRefreshEmitted = false;
};
