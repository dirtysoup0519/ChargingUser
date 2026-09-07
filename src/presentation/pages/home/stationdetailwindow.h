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
    // 集成期兼容旧 Demo；新 Binder 接通后不再连接此信号。
    void navigationRequested();
    // 充电合同冻结前保留但不由页面按钮发出。
    void chargeRequested();

private:
    void rebuildChargers(const QVector<ChargerListItemView> &chargers);

    Ui::StationDetailWindow *ui;
    class InteractiveMapWidget *m_map = nullptr;
    StationDetailViewState m_state;
};
