#include "tencentmapbridge.h"

#include <QJsonArray>
#include <QRegularExpression>

namespace {
bool validId(const QString &value)
{
    return !value.isEmpty() && value.size() <= 128
           && value.contains(QRegularExpression(QStringLiteral("^[A-Za-z0-9_.:-]+$")));
}

std::optional<GeoBounds> parseBounds(const QJsonObject &object)
{
    const auto sw = object.value(QStringLiteral("southWest")).toObject();
    const auto ne = object.value(QStringLiteral("northEast")).toObject();
    GeoBounds bounds{{sw.value(QStringLiteral("latitude")).toDouble(qQNaN()),
                      sw.value(QStringLiteral("longitude")).toDouble(qQNaN())},
                     {ne.value(QStringLiteral("latitude")).toDouble(qQNaN()),
                      ne.value(QStringLiteral("longitude")).toDouble(qQNaN())}};
    if (!bounds.isValid())
        return std::nullopt;
    return bounds;
}
}

TencentMapBridge::TencentMapBridge(QObject *parent) : QObject(parent) {}

void TencentMapBridge::reportReady()
{
    if (m_ready)
        return;
    m_ready = true;
    emit readyChanged(true);
    emit mapReady();
    if (!m_snapshot.isEmpty())
        emit snapshotChanged(m_snapshot);
}

void TencentMapBridge::reportLoadFailed(const QString &message)
{
    m_ready = false;
    emit readyChanged(false);
    emit mapLoadFailed(message.left(512));
}

void TencentMapBridge::selectStation(const QString &stationId)
{
    if (validId(stationId))
        emit stationSelected(stationId);
}

void TencentMapBridge::focusStation(const QString &stationId)
{
    if (validId(stationId))
        emit focusStationRequested(stationId);
}

void TencentMapBridge::reportViewport(const QJsonObject &bounds)
{
    const auto parsed = parseBounds(bounds);
    if (parsed)
        emit viewportChanged(*parsed);
}

void TencentMapBridge::setSnapshot(const QJsonObject &snapshot)
{
    QJsonObject sanitized;
    const QJsonArray inputMarkers = snapshot.value(QStringLiteral("markers")).toArray();
    QJsonArray markers;
    for (const QJsonValue &value : inputMarkers) {
        if (markers.size() >= 500 || !value.isObject()) break;
        const QJsonObject item = value.toObject();
        const QString stationId = item.value(QStringLiteral("stationId")).toString();
        const double latitude = item.value(QStringLiteral("latitude")).toDouble(qQNaN());
        const double longitude = item.value(QStringLiteral("longitude")).toDouble(qQNaN());
        GeoPoint point{latitude, longitude};
        if (!validId(stationId) || !point.isValid()) continue;
        QJsonObject clean;
        clean.insert(QStringLiteral("stationId"), stationId);
        clean.insert(QStringLiteral("latitude"), latitude);
        clean.insert(QStringLiteral("longitude"), longitude);
        clean.insert(QStringLiteral("available"), item.value(QStringLiteral("available")).toBool(true));
        markers.append(clean);
    }
    sanitized.insert(QStringLiteral("markers"), markers);
    const QJsonArray inputRoute = snapshot.value(QStringLiteral("routePolyline")).toArray();
    QJsonArray route;
    for (const QJsonValue &value : inputRoute) {
        if (route.size() >= 5000 || !value.isObject()) break;
        const QJsonObject item = value.toObject();
        GeoPoint point{item.value(QStringLiteral("latitude")).toDouble(qQNaN()),
                       item.value(QStringLiteral("longitude")).toDouble(qQNaN())};
        if (!point.isValid()) continue;
        route.append(QJsonObject{{QStringLiteral("latitude"), point.latitude},
                                 {QStringLiteral("longitude"), point.longitude}});
    }
    sanitized.insert(QStringLiteral("routePolyline"), route);
    sanitized.insert(QStringLiteral("selectedStationId"), snapshot.value(QStringLiteral("selectedStationId")).toString());
    m_snapshot = sanitized;
    if (m_ready)
        emit snapshotChanged(m_snapshot);
}

void TencentMapBridge::reset()
{
    if (!m_ready)
        return;
    m_ready = false;
    emit readyChanged(false);
}
