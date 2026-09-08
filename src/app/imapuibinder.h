#pragma once

#include "modules/map/maptypes.h"
#include "presentation/contracts/mapviewstates.h"

#include <QObject>
#include <QString>

enum class MapPageTarget
{
    Home,
    StationDetail,
    Navigation
};

/**
 * 地图页面与 M2/M4 的稳定装配边界。页面只连接同名用户意图槽并渲染状态信号；
 * Binder 不向页面暴露 Service、协议字段、QWebEngineView 或页面编号。
 */
class IMapUiBinder : public QObject
{
    Q_OBJECT

public:
    explicit IMapUiBinder(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~IMapUiBinder() override = default;

    virtual HomeMapViewState currentHomeState() const = 0;
    virtual StationDetailViewState currentStationDetailState() const = 0;
    virtual NavigationViewState currentNavigationState() const = 0;

public slots:
    /** 由 app/M4 在首页激活时调用，不是用户点击信号。 */
    virtual void activateHome() = 0;
    /** 以下三个入口来自受控地图展示桥，不直接触发站点业务查询。 */
    virtual void mapReady() = 0;
    virtual void mapLoadFailed() = 0;
    virtual void mapViewportChanged(const GeoBounds &bounds) = 0;
    virtual void locateRequested() = 0;
    virtual void stationSearchRequested(const QString &keyword) = 0;
    virtual void stationSearchRetryRequested() = 0;
    virtual void stationSearchCleared() = 0;
    virtual void searchAreaRequested(const GeoBounds &bounds) = 0;
    virtual void stationSelected(const QString &stationId) = 0;
    virtual void stationDetailsRequested(const QString &stationId) = 0;
    virtual void stationRefreshRequested() = 0;
    virtual void chargerSelected(const QString &chargerId) = 0;
    virtual void chargeConfirmationRequested(const QString &stationId,
                                             const QString &chargerId) = 0;
    virtual void routePreviewRequested(TravelMode mode) = 0;
    virtual void routeModeRequested(TravelMode mode) = 0;
    virtual void manualOriginRequested(const QString &address) = 0;
    virtual void originCandidateSelected(const QString &candidateId) = 0;
    virtual void routeRetryRequested() = 0;
    virtual void backRequested() = 0;

signals:
    void homeStateChanged(const HomeMapViewState &state);
    void stationDetailStateChanged(const StationDetailViewState &state);
    void navigationStateChanged(const NavigationViewState &state);
    void pageRequested(MapPageTarget target, const QString &stationId);
    /** 预留给后续 ChargingUiBinder；本阶段不创建订单或启动充电。 */
    void chargeConfirmationPageRequested(const QString &stationId,
                                         const QString &chargerId);
};

Q_DECLARE_METATYPE(MapPageTarget)
