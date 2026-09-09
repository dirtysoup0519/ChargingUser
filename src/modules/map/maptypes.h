#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QVector>

#include <cmath>
#include <limits>
#include <optional>

/** 已规范化的 GCJ-02 坐标，单位为度。未知位置必须使用 std::optional。 */
struct GeoPoint
{
    double latitude = std::numeric_limits<double>::quiet_NaN();
    double longitude = std::numeric_limits<double>::quiet_NaN();

    bool isValid() const
    {
        return std::isfinite(latitude) && std::isfinite(longitude)
               && latitude >= -90.0 && latitude <= 90.0
               && longitude >= -180.0 && longitude <= 180.0;
    }
};

/** GCJ-02 地图视野；首版不支持跨国际日期变更线。 */
struct GeoBounds
{
    GeoPoint southWest;
    GeoPoint northEast;

    bool isValid() const
    {
        return southWest.isValid() && northEast.isValid()
               && southWest.latitude <= northEast.latitude
               && southWest.longitude <= northEast.longitude;
    }
};

enum class LocationSource
{
    Device,
    Manual
};

/** 定位结果；capturedAtUtc 必须使用 UTC，精度未知时 accuracyMeters 为空。 */
struct LocationResult
{
    GeoPoint point;
    std::optional<double> accuracyMeters;
    QDateTime capturedAtUtc;
    LocationSource source = LocationSource::Device;
};

struct GeocodeCandidate
{
    QString candidateId;
    QString name;
    QString fullAddress;
    GeoPoint point;
};

struct GeocodeResult
{
    QVector<GeocodeCandidate> candidates;
};

enum class TravelMode
{
    Driving,
    Walking
};

/** 路线查询；目的地必须来自当前 stationId 对应的权威站点坐标。 */
struct RouteQuery
{
    GeoPoint origin;
    GeoPoint destination;
    QString stationId;
    TravelMode mode = TravelMode::Driving;
};

struct RouteStep
{
    QString instruction;
    std::optional<int> distanceMeters;
};

/** 规范化路线结果；polyline 为按行进方向排列的 GCJ-02 点。 */
struct RouteResult
{
    QString routeId;
    QString stationId;
    TravelMode mode = TravelMode::Driving;
    GeoPoint origin;
    GeoPoint destination;
    QVector<GeoPoint> polyline;
    int distanceMeters = 0;
    int durationSeconds = 0;
    QVector<RouteStep> steps;
};

Q_DECLARE_METATYPE(GeoPoint)
Q_DECLARE_METATYPE(GeoBounds)
Q_DECLARE_METATYPE(LocationSource)
Q_DECLARE_METATYPE(LocationResult)
Q_DECLARE_METATYPE(GeocodeCandidate)
Q_DECLARE_METATYPE(GeocodeResult)
Q_DECLARE_METATYPE(TravelMode)
Q_DECLARE_METATYPE(RouteQuery)
Q_DECLARE_METATYPE(RouteStep)
Q_DECLARE_METATYPE(RouteResult)
