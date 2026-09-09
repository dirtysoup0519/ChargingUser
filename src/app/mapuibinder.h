#pragma once

#include "app/imapuibinder.h"
#include "common/clienterror.h"
#include "common/requestcontext.h"
#include "modules/charger/chargertypes.h"

#include <QHash>
#include <optional>

class IChargerService;
class IMapService;
class QTimer;

/**
 * 地图、站点详情与路线预览的应用编排器。
 *
 * 本类是页面与 M2 服务之间的唯一连接点：生成只读 requestId、保存页面上下文，
 * 并且只接受当前请求的终态。它不依赖 QWidget，也不解析网络或地图供应商协议。
 */
class MapUiBinder final : public IMapUiBinder
{
    Q_OBJECT

public:
    explicit MapUiBinder(IChargerService *chargerService,
                         IMapService *mapService,
                         QObject *parent = nullptr);

    HomeMapViewState currentHomeState() const override;
    StationDetailViewState currentStationDetailState() const override;
    NavigationViewState currentNavigationState() const override;

    void setActiveReservation(const std::optional<ActiveReservationView> &reservation);

public slots:
    void activateHome() override;
    void mapReady() override;
    void mapLoadFailed() override;
    void mapViewportChanged(const GeoBounds &bounds) override;
    void locateRequested() override;
    void stationSearchRequested(const QString &keyword) override;
    void stationSearchRetryRequested() override;
    void stationSearchCleared() override;
    void searchAreaRequested(const GeoBounds &bounds) override;
    void stationSelected(const QString &stationId) override;
    void stationDetailsRequested(const QString &stationId) override;
    void stationRefreshRequested() override;
    void chargerSelected(const QString &chargerId) override;
    void chargerStatusConfirmed(const QString &stationId,
                                const QString &chargerId,
                                ChargerBusinessStatus status) override;
    void chargeConfirmationRequested(const QString &stationId,
                                     const QString &chargerId) override;
    void routePreviewRequested(TravelMode mode) override;
    void routeModeRequested(TravelMode mode) override;
    void manualOriginRequested(const QString &address) override;
    void originCandidateSelected(const QString &candidateId) override;
    void routeRetryRequested() override;
    void backRequested() override;

private slots:
    void autoRefresh();
    void handleStationsReady(const RequestContext &context,
                             const StationPage &page);
    void handleStationDetailReady(const RequestContext &context,
                                  const StationDetail &detail);
    void handleLocationReady(const RequestContext &context,
                             const LocationResult &result);
    void handleGeocodeReady(const RequestContext &context,
                            const GeocodeResult &result);
    void handleRouteReady(const RequestContext &context,
                          const RouteResult &result);
    void handleChargerRequestFailed(const ClientError &error);
    void handleMapRequestFailed(const ClientError &error);

private:
    RequestContext createContext(const QString &prefix) const;
    void publishHome();
    void publishDetail();
    void publishNavigation();
    bool makeAreaQuery(StationQuery *query) const;
    void startStationQuery(const StationQuery &query);
    void startDetailQuery(const QString &stationId, bool preserveContent);
    void startRouteQuery();
    void cancelRequest(QString *requestId, QObject *service);
    void rebuildHomeResults(const StationPage &page);
    void applyStationSelection(const QString &stationId);
    void clearNavigationRequestState();

    IChargerService *m_chargerService;
    IMapService *m_mapService;
    HomeMapViewState m_home;
    StationDetailViewState m_detail;
    NavigationViewState m_navigation;
    MapPageTarget m_currentPage = MapPageTarget::Home;
    QHash<QString, StationSummary> m_stationsById;
    QHash<QString, GeoPoint> m_candidatePoints;
    QHash<QString, ChargerBusinessStatus> m_confirmedChargerStatuses;
    std::optional<StationQuery> m_lastStationQuery;
    QString m_stationsRequestId;
    QString m_detailRequestId;
    QString m_locationRequestId;
    QString m_geocodeRequestId;
    QString m_routeRequestId;
    quint64 m_cameraRevision = 0;
    QTimer *m_autoRefreshTimer = nullptr;
};
