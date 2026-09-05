#pragma once

#include "modules/charger/chargertypes.h"
#include "modules/map/maptypes.h"

#include <QMetaType>
#include <QString>
#include <QVector>

#include <optional>

enum class MapLoadStatus
{
    Idle,
    Loading,
    Ready,
    Empty,
    Error
};

struct MapCameraView
{
    std::optional<GeoPoint> center;
    std::optional<GeoBounds> bounds;
    std::optional<double> zoom;
};

enum class MapCameraCommandType
{
    None,
    CenterStation,
    FitStations
};

/** 一次性地图视野命令；UI 仅在 revision 增加时执行。 */
struct MapCameraCommand
{
    MapCameraCommandType type = MapCameraCommandType::None;
    QString stationId;
    QVector<QString> stationIds;
    quint64 revision = 0;
};

struct MapMarkerView
{
    QString stationId;
    QString title;
    GeoPoint point;
    bool selected = false;
};

struct StationListItemView
{
    QString stationId;
    QString name;
    QString address;
    QString distanceText;
    QString availabilityText;
    QString priceText;
    bool selected = false;
};

struct ChargerListItemView
{
    QString chargerId;
    QString title;
    QString statusText;
    QString powerText;
    bool canCharge = false;
    QString disabledReason;
};

struct GeocodeCandidateView
{
    QString candidateId;
    QString name;
    QString fullAddress;
};

struct RouteStepView
{
    QString instruction;
    QString distanceText;
};

struct RouteViewData
{
    QString routeId;
    QVector<GeoPoint> polyline;
    QString distanceText;
    QString durationText;
    QVector<RouteStepView> steps;
};

/** 首页地图、定位和站点列表分别维护状态，任一失败不得覆盖其他区域。 */
struct HomeMapViewState
{
    MapLoadStatus mapStatus = MapLoadStatus::Idle;
    MapLoadStatus locationStatus = MapLoadStatus::Idle;
    MapLoadStatus searchStatus = MapLoadStatus::Idle;
    MapLoadStatus stationsStatus = MapLoadStatus::Idle;
    QVector<MapMarkerView> markers;
    QVector<StationListItemView> stations;
    QString selectedStationId;
    MapCameraView camera;
    MapCameraCommand cameraCommand;
    std::optional<GeoPoint> currentLocation;
    QString queryInput;
    QString submittedQuery;
    QString mapMessage;
    QString locationMessage;
    QString searchMessage;
    QString stationsMessage;
    bool canSearch = true;
    bool canRetrySearch = false;
    bool canRetryMap = false;
    bool canRetryLocation = false;
    bool canRetryStations = false;
    bool canSearchCurrentArea = false;
};

struct StationDetailViewState
{
    QString stationId;
    MapLoadStatus status = MapLoadStatus::Idle;
    QString name;
    QString address;
    QString availabilityText;
    QString priceText;
    QVector<ChargerListItemView> chargers;
    QString message;
    bool canRetry = false;
    bool canNavigate = false;
    bool canCharge = false;
    QString disabledReason;
};

struct NavigationViewState
{
    QString stationId;
    QString originText;
    QString destinationText;
    std::optional<GeoPoint> origin;
    std::optional<GeoPoint> destination;
    TravelMode mode = TravelMode::Driving;
    MapLoadStatus routeStatus = MapLoadStatus::Idle;
    std::optional<RouteViewData> route;
    QVector<GeocodeCandidateView> originCandidates;
    QString message;
    bool canRetry = false;
    bool canChangeMode = true;
};

Q_DECLARE_METATYPE(MapLoadStatus)
Q_DECLARE_METATYPE(MapCameraView)
Q_DECLARE_METATYPE(MapCameraCommandType)
Q_DECLARE_METATYPE(MapCameraCommand)
Q_DECLARE_METATYPE(MapMarkerView)
Q_DECLARE_METATYPE(StationListItemView)
Q_DECLARE_METATYPE(ChargerListItemView)
Q_DECLARE_METATYPE(GeocodeCandidateView)
Q_DECLARE_METATYPE(RouteStepView)
Q_DECLARE_METATYPE(RouteViewData)
Q_DECLARE_METATYPE(HomeMapViewState)
Q_DECLARE_METATYPE(StationDetailViewState)
Q_DECLARE_METATYPE(NavigationViewState)
