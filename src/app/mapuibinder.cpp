#include "app/mapuibinder.h"

#include "modules/charger/ichargerservice.h"
#include "modules/map/imapservice.h"

#include <QMetaType>
#include <QUuid>

#include <algorithm>

namespace {

QString distanceText(const std::optional<int> &meters)
{
    if (!meters || *meters < 0) {
        return QStringLiteral("距离未知");
    }
    if (*meters < 1000) {
        return QStringLiteral("%1 m").arg(*meters);
    }
    return QStringLiteral("%1 km").arg(*meters / 1000.0, 0, 'f', 1);
}

QString durationText(int seconds)
{
    const int minutes = std::max(1, (seconds + 59) / 60);
    if (minutes < 60) {
        return QStringLiteral("约 %1 分钟").arg(minutes);
    }
    const int hours = minutes / 60;
    const int remainingMinutes = minutes % 60;
    return remainingMinutes == 0
               ? QStringLiteral("约 %1 小时").arg(hours)
               : QStringLiteral("约 %1 小时 %2 分钟").arg(hours).arg(remainingMinutes);
}

QString priceText(const std::optional<qint64> &centsPerKwh)
{
    if (!centsPerKwh || *centsPerKwh < 0) {
        return QStringLiteral("价格待确认");
    }
    return QStringLiteral("¥%1/度").arg(*centsPerKwh / 100.0, 0, 'f', 2);
}

QString availabilityText(int available, int total)
{
    return QStringLiteral("可用 %1/%2").arg(std::max(0, available))
        .arg(std::max(0, total));
}

QString chargerStatusText(const ChargerSummary &charger)
{
    if (!charger.online) {
        return QStringLiteral("离线");
    }
    switch (charger.businessStatus) {
    case ChargerBusinessStatus::Idle:
        return QStringLiteral("空闲");
    case ChargerBusinessStatus::Reserved:
        return QStringLiteral("已预约");
    case ChargerBusinessStatus::Charging:
        return QStringLiteral("使用中");
    case ChargerBusinessStatus::Fault:
        return QStringLiteral("故障");
    case ChargerBusinessStatus::Restarting:
        return QStringLiteral("重启中");
    case ChargerBusinessStatus::Unknown:
        return QStringLiteral("状态未知");
    }
    return QStringLiteral("状态未知");
}

QString displayError(const ClientError &error, const QString &fallback)
{
    return error.displayMessage.isEmpty() ? fallback : error.displayMessage;
}

} // namespace

MapUiBinder::MapUiBinder(IChargerService *chargerService,
                         IMapService *mapService,
                         QObject *parent)
    : IMapUiBinder(parent)
    , m_chargerService(chargerService)
    , m_mapService(mapService)
{
    Q_ASSERT(m_chargerService);
    Q_ASSERT(m_mapService);

    qRegisterMetaType<HomeMapViewState>();
    qRegisterMetaType<StationDetailViewState>();
    qRegisterMetaType<NavigationViewState>();
    qRegisterMetaType<MapPageTarget>();

    connect(m_chargerService, &IChargerService::stationsReady,
            this, &MapUiBinder::handleStationsReady);
    connect(m_chargerService, &IChargerService::stationDetailReady,
            this, &MapUiBinder::handleStationDetailReady);
    connect(m_chargerService, &IChargerService::requestFailed,
            this, &MapUiBinder::handleChargerRequestFailed);
    connect(m_mapService, &IMapService::locationReady,
            this, &MapUiBinder::handleLocationReady);
    connect(m_mapService, &IMapService::geocodeReady,
            this, &MapUiBinder::handleGeocodeReady);
    connect(m_mapService, &IMapService::routeReady,
            this, &MapUiBinder::handleRouteReady);
    connect(m_mapService, &IMapService::requestFailed,
            this, &MapUiBinder::handleMapRequestFailed);
}

HomeMapViewState MapUiBinder::currentHomeState() const
{
    return m_home;
}

StationDetailViewState MapUiBinder::currentStationDetailState() const
{
    return m_detail;
}

NavigationViewState MapUiBinder::currentNavigationState() const
{
    return m_navigation;
}

void MapUiBinder::activateHome()
{
    m_currentPage = MapPageTarget::Home;
    emit pageRequested(MapPageTarget::Home, m_home.selectedStationId);
    publishHome();
    if (!m_home.currentLocation && m_locationRequestId.isEmpty()) {
        locateRequested();
    } else if (m_home.stationsStatus == MapLoadStatus::Idle) {
        StationQuery query;
        if (makeAreaQuery(&query)) {
            startStationQuery(query);
        }
    }
}

void MapUiBinder::mapReady()
{
    m_home.mapStatus = MapLoadStatus::Ready;
    m_home.mapMessage.clear();
    m_home.canRetryMap = false;
    publishHome();
}

void MapUiBinder::mapLoadFailed()
{
    m_home.mapStatus = MapLoadStatus::Error;
    m_home.mapMessage = QStringLiteral("地图暂时无法显示，站点文字列表仍可使用。");
    m_home.canRetryMap = true;
    publishHome();
}

void MapUiBinder::mapViewportChanged(const GeoBounds &bounds)
{
    if (!bounds.isValid()) {
        return;
    }
    m_home.camera.bounds = bounds;
    m_home.canSearchCurrentArea = true;
    publishHome();
}

void MapUiBinder::locateRequested()
{
    cancelRequest(&m_locationRequestId, m_mapService);
    const RequestContext context = createContext(QStringLiteral("location"));
    m_locationRequestId = context.requestId;
    m_home.locationStatus = MapLoadStatus::Loading;
    m_home.locationMessage = QStringLiteral("正在获取当前位置…");
    m_home.canRetryLocation = false;
    publishHome();
    m_mapService->locate(context);
}

void MapUiBinder::stationSearchRequested(const QString &keyword)
{
    const QString trimmed = keyword.trimmed();
    m_home.queryInput = trimmed;
    if (trimmed.isEmpty()) {
        stationSearchCleared();
        return;
    }

    StationQuery query;
    if (!makeAreaQuery(&query)) {
        m_home.searchStatus = MapLoadStatus::Error;
        m_home.searchMessage = QStringLiteral("请先定位或选择有效地图区域。");
        m_home.canSearch = true;
        m_home.canRetrySearch = false;
        publishHome();
        return;
    }
    query.keyword = trimmed;
    m_home.submittedQuery = trimmed;
    startStationQuery(query);
}

void MapUiBinder::stationSearchRetryRequested()
{
    if (!m_lastStationQuery) {
        return;
    }
    startStationQuery(*m_lastStationQuery);
}

void MapUiBinder::stationSearchCleared()
{
    m_home.queryInput.clear();
    m_home.submittedQuery.clear();
    StationQuery query;
    if (makeAreaQuery(&query)) {
        startStationQuery(query);
    } else {
        m_home.searchStatus = MapLoadStatus::Idle;
        m_home.searchMessage.clear();
        m_home.canSearch = true;
        publishHome();
    }
}

void MapUiBinder::searchAreaRequested(const GeoBounds &bounds)
{
    if (!bounds.isValid()) {
        m_home.searchStatus = MapLoadStatus::Error;
        m_home.searchMessage = QStringLiteral("当前地图区域无效，请重新选择。");
        m_home.canRetrySearch = false;
        publishHome();
        return;
    }
    m_home.camera.bounds = bounds;
    m_home.canSearchCurrentArea = false;
    StationQuery query;
    query.bounds = bounds;
    query.keyword = m_home.submittedQuery;
    startStationQuery(query);
}

void MapUiBinder::stationSelected(const QString &stationId)
{
    if (!m_stationsById.contains(stationId)) {
        return;
    }
    applyStationSelection(stationId);
    publishHome();
}

void MapUiBinder::stationDetailsRequested(const QString &stationId)
{
    if (stationId.trimmed().isEmpty()) {
        return;
    }
    m_currentPage = MapPageTarget::StationDetail;
    startDetailQuery(stationId, false);
    emit pageRequested(MapPageTarget::StationDetail, stationId);
}

void MapUiBinder::stationRefreshRequested()
{
    if (m_detail.stationId.isEmpty()
        || m_detail.status == MapLoadStatus::Loading) {
        return;
    }
    startDetailQuery(m_detail.stationId, true);
}

void MapUiBinder::routePreviewRequested(TravelMode mode)
{
    if (m_detail.status != MapLoadStatus::Ready
        || !m_detail.canNavigate
        || !m_stationsById.contains(m_detail.stationId)
        || !m_stationsById.value(m_detail.stationId).point) {
        return;
    }

    clearNavigationRequestState();
    const StationSummary station = m_stationsById.value(m_detail.stationId);
    m_navigation.stationId = station.stationId;
    m_navigation.destination = station.point;
    m_navigation.destinationText = station.name;
    m_navigation.mode = mode;
    m_navigation.origin = m_home.currentLocation;
    m_navigation.originText = m_home.currentLocation
                                  ? QStringLiteral("当前位置")
                                  : QString();
    m_navigation.routeStatus = MapLoadStatus::Idle;
    m_navigation.canChangeMode = true;
    m_currentPage = MapPageTarget::Navigation;
    emit pageRequested(MapPageTarget::Navigation, station.stationId);
    if (m_navigation.origin) {
        startRouteQuery();
    } else {
        m_navigation.message = QStringLiteral("请输入起点地址后预览路线。");
        publishNavigation();
    }
}

void MapUiBinder::routeModeRequested(TravelMode mode)
{
    if (mode == m_navigation.mode || !m_navigation.canChangeMode) {
        return;
    }
    m_navigation.mode = mode;
    if (m_navigation.origin && m_navigation.destination) {
        startRouteQuery();
    } else {
        publishNavigation();
    }
}

void MapUiBinder::manualOriginRequested(const QString &address)
{
    const QString trimmed = address.trimmed();
    if (trimmed.isEmpty() || m_navigation.stationId.isEmpty()) {
        return;
    }
    cancelRequest(&m_geocodeRequestId, m_mapService);
    cancelRequest(&m_routeRequestId, m_mapService);
    const RequestContext context = createContext(QStringLiteral("geocode"));
    m_geocodeRequestId = context.requestId;
    m_navigation.originText = trimmed;
    m_navigation.origin.reset();
    m_navigation.originCandidates.clear();
    m_candidatePoints.clear();
    m_navigation.routeStatus = MapLoadStatus::Loading;
    m_navigation.route.reset();
    m_navigation.message = QStringLiteral("正在解析起点地址…");
    m_navigation.canRetry = false;
    m_navigation.canChangeMode = false;
    publishNavigation();
    m_mapService->geocode(context, trimmed);
}

void MapUiBinder::originCandidateSelected(const QString &candidateId)
{
    if (!m_candidatePoints.contains(candidateId)) {
        return;
    }
    m_navigation.origin = m_candidatePoints.value(candidateId);
    for (const GeocodeCandidateView &candidate : m_navigation.originCandidates) {
        if (candidate.candidateId == candidateId) {
            m_navigation.originText = candidate.fullAddress.isEmpty()
                                          ? candidate.name
                                          : candidate.fullAddress;
            break;
        }
    }
    m_navigation.originCandidates.clear();
    m_candidatePoints.clear();
    startRouteQuery();
}

void MapUiBinder::routeRetryRequested()
{
    if (m_navigation.canRetry
        && m_navigation.origin && m_navigation.destination) {
        startRouteQuery();
    }
}

void MapUiBinder::backRequested()
{
    if (m_currentPage == MapPageTarget::Navigation) {
        cancelRequest(&m_geocodeRequestId, m_mapService);
        cancelRequest(&m_routeRequestId, m_mapService);
        m_currentPage = MapPageTarget::StationDetail;
        emit pageRequested(MapPageTarget::StationDetail, m_detail.stationId);
        publishDetail();
        return;
    }
    if (m_currentPage == MapPageTarget::StationDetail) {
        cancelRequest(&m_detailRequestId, m_chargerService);
        m_currentPage = MapPageTarget::Home;
        emit pageRequested(MapPageTarget::Home, m_home.selectedStationId);
        publishHome();
    }
}

void MapUiBinder::handleStationsReady(const RequestContext &context,
                                      const StationPage &page)
{
    if (context.requestId != m_stationsRequestId) {
        return;
    }
    m_stationsRequestId.clear();
    rebuildHomeResults(page);
    m_home.stationsStatus = page.items.isEmpty()
                                ? MapLoadStatus::Empty
                                : MapLoadStatus::Ready;
    m_home.searchStatus = m_home.stationsStatus;
    m_home.stationsMessage = page.items.isEmpty()
                                 ? QStringLiteral("未找到相关充电站。")
                                 : QString();
    m_home.searchMessage.clear();
    m_home.canSearch = true;
    m_home.canRetrySearch = false;
    m_home.canRetryStations = false;
    publishHome();
}

void MapUiBinder::handleStationDetailReady(const RequestContext &context,
                                           const StationDetail &detail)
{
    if (context.requestId != m_detailRequestId
        || detail.stationId != m_detail.stationId) {
        return;
    }
    m_detailRequestId.clear();
    StationSummary summary = detail.summary;
    summary.stationId = detail.stationId;
    m_stationsById.insert(detail.stationId, summary);
    m_detail.name = summary.name;
    m_detail.address = summary.address;
    m_detail.availabilityText = availabilityText(summary.availableCount,
                                                  summary.totalCount);
    m_detail.priceText = priceText(summary.priceCentsPerKwh);
    m_detail.chargers.clear();
    bool canCharge = false;
    QString disabledReason;
    for (const ChargerSummary &charger : detail.chargers) {
        if (charger.chargerId.isEmpty()) {
            continue;
        }
        ChargerListItemView item;
        item.chargerId = charger.chargerId;
        item.title = charger.type.isEmpty()
                         ? QStringLiteral("充电桩")
                         : charger.type;
        item.statusText = chargerStatusText(charger);
        item.powerText = charger.powerKw
                             ? QStringLiteral("%1 kW").arg(*charger.powerKw, 0, 'f', 1)
                             : QStringLiteral("功率未知");
        item.canCharge = charger.canStartCharging;
        item.disabledReason = charger.disabledReason;
        canCharge = canCharge || item.canCharge;
        if (disabledReason.isEmpty() && !item.disabledReason.isEmpty()) {
            disabledReason = item.disabledReason;
        }
        m_detail.chargers.append(item);
    }
    // 详情本身已成功时保持 Ready；没有充电桩不应阻断使用站点坐标预览路线。
    m_detail.status = MapLoadStatus::Ready;
    m_detail.message = m_detail.chargers.isEmpty()
                           ? QStringLiteral("该站点暂无充电桩信息。")
                           : QString();
    m_detail.canRetry = false;
    m_detail.canNavigate = summary.point && summary.point->isValid();
    m_detail.canCharge = canCharge;
    if (!m_detail.canNavigate) {
        m_detail.disabledReason = QStringLiteral("站点坐标缺失，暂不能规划路线。");
    } else if (!canCharge) {
        m_detail.disabledReason = disabledReason.isEmpty()
                                      ? QStringLiteral("当前没有可启动的充电桩。")
                                      : disabledReason;
    } else {
        m_detail.disabledReason.clear();
    }
    publishDetail();
}

void MapUiBinder::handleLocationReady(const RequestContext &context,
                                      const LocationResult &result)
{
    if (context.requestId != m_locationRequestId || !result.point.isValid()) {
        return;
    }
    m_locationRequestId.clear();
    m_home.currentLocation = result.point;
    m_home.locationStatus = MapLoadStatus::Ready;
    m_home.locationMessage.clear();
    m_home.canRetryLocation = false;
    publishHome();

    if (m_home.stationsStatus == MapLoadStatus::Idle) {
        StationQuery query;
        query.center = result.point;
        startStationQuery(query);
    }
}

void MapUiBinder::handleGeocodeReady(const RequestContext &context,
                                     const GeocodeResult &result)
{
    if (context.requestId != m_geocodeRequestId) {
        return;
    }
    m_geocodeRequestId.clear();
    m_navigation.canChangeMode = true;
    m_navigation.originCandidates.clear();
    m_candidatePoints.clear();
    for (const GeocodeCandidate &candidate : result.candidates) {
        if (candidate.candidateId.isEmpty() || !candidate.point.isValid()
            || m_candidatePoints.contains(candidate.candidateId)) {
            continue;
        }
        m_candidatePoints.insert(candidate.candidateId, candidate.point);
        m_navigation.originCandidates.append({candidate.candidateId,
                                               candidate.name,
                                               candidate.fullAddress});
    }
    if (m_navigation.originCandidates.isEmpty()) {
        m_navigation.routeStatus = MapLoadStatus::Empty;
        m_navigation.message = QStringLiteral("未找到该起点，请尝试更完整的地址。");
        m_navigation.canRetry = false;
        publishNavigation();
        return;
    }
    if (m_navigation.originCandidates.size() == 1) {
        originCandidateSelected(m_navigation.originCandidates.first().candidateId);
        return;
    }
    m_navigation.routeStatus = MapLoadStatus::Idle;
    m_navigation.message = QStringLiteral("请选择准确的起点。");
    m_navigation.canRetry = false;
    publishNavigation();
}

void MapUiBinder::handleRouteReady(const RequestContext &context,
                                   const RouteResult &result)
{
    if (context.requestId != m_routeRequestId
        || result.stationId != m_navigation.stationId
        || result.mode != m_navigation.mode) {
        return;
    }
    m_routeRequestId.clear();
    m_navigation.canChangeMode = true;
    m_navigation.canRetry = false;
    if (result.polyline.isEmpty()) {
        m_navigation.routeStatus = MapLoadStatus::Empty;
        m_navigation.route.reset();
        m_navigation.message = QStringLiteral("暂无可用路线，请更换起点或出行方式。");
        publishNavigation();
        return;
    }

    RouteViewData route;
    route.routeId = result.routeId;
    route.polyline = result.polyline;
    route.distanceText = distanceText(result.distanceMeters);
    route.durationText = durationText(result.durationSeconds);
    for (const RouteStep &step : result.steps) {
        if (step.instruction.trimmed().isEmpty()) {
            continue;
        }
        route.steps.append({step.instruction, distanceText(step.distanceMeters)});
    }
    m_navigation.origin = result.origin;
    m_navigation.destination = result.destination;
    m_navigation.route = route;
    m_navigation.routeStatus = MapLoadStatus::Ready;
    m_navigation.message.clear();
    publishNavigation();
}

void MapUiBinder::handleChargerRequestFailed(const ClientError &error)
{
    if (error.requestId == m_stationsRequestId) {
        m_stationsRequestId.clear();
        m_home.stationsStatus = MapLoadStatus::Error;
        m_home.searchStatus = MapLoadStatus::Error;
        m_home.searchMessage = displayError(error,
                                             QStringLiteral("站点查询失败。"));
        m_home.canSearch = true;
        m_home.canRetrySearch = error.retryable;
        m_home.canRetryStations = error.retryable;
        publishHome();
        return;
    }
    if (error.requestId == m_detailRequestId) {
        m_detailRequestId.clear();
        m_detail.status = MapLoadStatus::Error;
        m_detail.message = displayError(error,
                                         QStringLiteral("站点详情加载失败。"));
        m_detail.canRetry = error.retryable;
        m_detail.canNavigate = false;
        m_detail.canCharge = false;
        publishDetail();
    }
}

void MapUiBinder::handleMapRequestFailed(const ClientError &error)
{
    if (error.requestId == m_locationRequestId) {
        m_locationRequestId.clear();
        m_home.locationStatus = MapLoadStatus::Error;
        m_home.locationMessage = displayError(error,
                                               QStringLiteral("无法获取当前位置。"));
        m_home.canRetryLocation = error.retryable;
        publishHome();
        return;
    }
    if (error.requestId == m_geocodeRequestId) {
        m_geocodeRequestId.clear();
        m_navigation.routeStatus = MapLoadStatus::Error;
        m_navigation.message = displayError(error,
                                             QStringLiteral("起点地址解析失败。"));
        m_navigation.canRetry = false;
        m_navigation.canChangeMode = true;
        publishNavigation();
        return;
    }
    if (error.requestId == m_routeRequestId) {
        m_routeRequestId.clear();
        m_navigation.routeStatus = MapLoadStatus::Error;
        m_navigation.message = displayError(error,
                                             QStringLiteral("路线规划失败。"));
        m_navigation.canRetry = error.retryable;
        m_navigation.canChangeMode = true;
        publishNavigation();
    }
}

RequestContext MapUiBinder::createContext(const QString &prefix) const
{
    RequestContext context;
    context.requestId = prefix + QLatin1Char('-')
                        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    return context;
}

void MapUiBinder::publishHome()
{
    emit homeStateChanged(m_home);
}

void MapUiBinder::publishDetail()
{
    emit stationDetailStateChanged(m_detail);
}

void MapUiBinder::publishNavigation()
{
    emit navigationStateChanged(m_navigation);
}

bool MapUiBinder::makeAreaQuery(StationQuery *query) const
{
    if (!query) {
        return false;
    }
    *query = StationQuery{};
    if (m_home.camera.bounds && m_home.camera.bounds->isValid()) {
        query->bounds = m_home.camera.bounds;
        return true;
    }
    if (m_home.currentLocation && m_home.currentLocation->isValid()) {
        query->center = m_home.currentLocation;
        return true;
    }
    return false;
}

void MapUiBinder::startStationQuery(const StationQuery &query)
{
    cancelRequest(&m_stationsRequestId, m_chargerService);
    const RequestContext context = createContext(QStringLiteral("stations"));
    m_stationsRequestId = context.requestId;
    m_lastStationQuery = query;
    m_home.stationsStatus = MapLoadStatus::Loading;
    m_home.searchStatus = MapLoadStatus::Loading;
    m_home.stationsMessage = QStringLiteral("正在加载充电站…");
    m_home.searchMessage.clear();
    m_home.canSearch = false;
    m_home.canRetrySearch = false;
    m_home.canRetryStations = false;
    publishHome();
    m_chargerService->queryStations(context, query);
}

void MapUiBinder::startDetailQuery(const QString &stationId, bool preserveContent)
{
    cancelRequest(&m_detailRequestId, m_chargerService);
    if (!preserveContent || m_detail.stationId != stationId) {
        m_detail = StationDetailViewState{};
        m_detail.stationId = stationId;
    }
    const RequestContext context = createContext(QStringLiteral("station-detail"));
    m_detailRequestId = context.requestId;
    m_detail.status = MapLoadStatus::Loading;
    m_detail.message = preserveContent
                           ? QStringLiteral("正在刷新，当前内容可能不是最新数据…")
                           : QStringLiteral("正在加载充电站详情…");
    m_detail.canRetry = false;
    m_detail.canNavigate = false;
    publishDetail();
    m_chargerService->queryStationDetail(context, stationId);
}

void MapUiBinder::startRouteQuery()
{
    if (!m_navigation.origin || !m_navigation.destination
        || !m_navigation.origin->isValid()
        || !m_navigation.destination->isValid()
        || m_navigation.stationId.isEmpty()) {
        return;
    }
    cancelRequest(&m_routeRequestId, m_mapService);
    RouteQuery query;
    query.origin = *m_navigation.origin;
    query.destination = *m_navigation.destination;
    query.stationId = m_navigation.stationId;
    query.mode = m_navigation.mode;
    const RequestContext context = createContext(QStringLiteral("route"));
    m_routeRequestId = context.requestId;
    m_navigation.routeStatus = MapLoadStatus::Loading;
    m_navigation.message = QStringLiteral("正在规划路线…");
    m_navigation.canRetry = false;
    m_navigation.canChangeMode = false;
    publishNavigation();
    m_mapService->planRoute(context, query);
}

void MapUiBinder::cancelRequest(QString *requestId, QObject *service)
{
    if (!requestId || requestId->isEmpty()) {
        return;
    }
    if (service == m_chargerService) {
        m_chargerService->cancel(*requestId);
    } else if (service == m_mapService) {
        m_mapService->cancel(*requestId);
    }
    requestId->clear();
}

void MapUiBinder::rebuildHomeResults(const StationPage &page)
{
    m_home.stations.clear();
    m_home.markers.clear();
    m_stationsById.clear();
    QVector<QString> validMarkerIds;
    for (const StationSummary &station : page.items) {
        if (station.stationId.isEmpty() || m_stationsById.contains(station.stationId)) {
            continue;
        }
        m_stationsById.insert(station.stationId, station);
        m_home.stations.append({station.stationId,
                                station.name,
                                station.address,
                                distanceText(station.distanceMeters),
                                availabilityText(station.availableCount,
                                                 station.totalCount),
                                priceText(station.priceCentsPerKwh),
                                false});
        if (station.point && station.point->isValid()) {
            m_home.markers.append({station.stationId,
                                   station.name,
                                   *station.point,
                                   false});
            validMarkerIds.append(station.stationId);
        }
    }

    if (!m_home.selectedStationId.isEmpty()
        && m_stationsById.contains(m_home.selectedStationId)) {
        applyStationSelection(m_home.selectedStationId);
    } else {
        m_home.selectedStationId.clear();
    }

    if (m_home.stations.size() == 1 && !validMarkerIds.isEmpty()) {
        applyStationSelection(m_home.stations.first().stationId);
    } else if (validMarkerIds.size() > 1) {
        m_home.cameraCommand.type = MapCameraCommandType::FitStations;
        m_home.cameraCommand.stationIds = validMarkerIds;
        m_home.cameraCommand.stationId.clear();
        m_home.cameraCommand.revision = ++m_cameraRevision;
    }
}

void MapUiBinder::applyStationSelection(const QString &stationId)
{
    m_home.selectedStationId = stationId;
    for (StationListItemView &station : m_home.stations) {
        station.selected = station.stationId == stationId;
    }
    for (MapMarkerView &marker : m_home.markers) {
        marker.selected = marker.stationId == stationId;
    }
    const auto iterator = std::find_if(
        m_home.stations.begin(), m_home.stations.end(),
        [&stationId](const StationListItemView &station) {
            return station.stationId == stationId;
        });
    if (iterator != m_home.stations.end() && iterator != m_home.stations.begin()) {
        const StationListItemView selected = *iterator;
        m_home.stations.erase(iterator);
        m_home.stations.prepend(selected);
    }
    if (m_stationsById.contains(stationId)
        && m_stationsById.value(stationId).point) {
        m_home.cameraCommand.type = MapCameraCommandType::CenterStation;
        m_home.cameraCommand.stationId = stationId;
        m_home.cameraCommand.stationIds.clear();
        m_home.cameraCommand.revision = ++m_cameraRevision;
    }
}

void MapUiBinder::clearNavigationRequestState()
{
    cancelRequest(&m_geocodeRequestId, m_mapService);
    cancelRequest(&m_routeRequestId, m_mapService);
    m_navigation = NavigationViewState{};
    m_candidatePoints.clear();
}
