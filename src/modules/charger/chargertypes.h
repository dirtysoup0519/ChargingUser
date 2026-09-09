#pragma once

#include "modules/map/maptypes.h"

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QVector>

#include <optional>

enum class ChargerBusinessStatus
{
    Idle,
    Reserved,
    Charging,
    Fault,
    Restarting,
    Unknown
};

/** 电桩网络在线状态与业务状态必须分别保存，不能相互推导。 */
struct ChargerSummary
{
    QString chargerId;
    QString type;
    std::optional<double> powerKw;
    ChargerBusinessStatus businessStatus = ChargerBusinessStatus::Unknown;
    bool online = false;
    bool canStartCharging = false;
    QString disabledReason;
};

struct StationQuery
{
    std::optional<GeoPoint> center;
    std::optional<GeoBounds> bounds;
    QString keyword;
    QString cursor;
    int pageSize = 20;

    bool hasExactlyOneArea() const
    {
        return center.has_value() != bounds.has_value();
    }

    bool hasAtMostOneArea() const
    {
        return !(center.has_value() && bounds.has_value());
    }
};

struct StationSummary
{
    QString stationId;
    QString name;
    QString address;
    std::optional<GeoPoint> point;
    std::optional<int> distanceMeters;
    int availableCount = 0;
    int totalCount = 0;
    std::optional<qint64> priceCentsPerKwh;
};

struct StationPage
{
    QVector<StationSummary> items;
    QString nextCursor;
    bool hasMore = false;
};

struct StationDetail
{
    QString stationId;
    StationSummary summary;
    QVector<ChargerSummary> chargers;
    QDateTime updatedAtUtc;
};

Q_DECLARE_METATYPE(ChargerBusinessStatus)
Q_DECLARE_METATYPE(ChargerSummary)
Q_DECLARE_METATYPE(StationQuery)
Q_DECLARE_METATYPE(StationSummary)
Q_DECLARE_METATYPE(StationPage)
Q_DECLARE_METATYPE(StationDetail)
