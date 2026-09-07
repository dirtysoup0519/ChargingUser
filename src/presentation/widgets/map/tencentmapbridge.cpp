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

void TencentMapBridge::reportViewport(const QJsonObject &bounds)
{
    const auto parsed = parseBounds(bounds);
    if (parsed)
        emit viewportChanged(*parsed);
}

void TencentMapBridge::setSnapshot(const QJsonObject &snapshot)
{
    m_snapshot = snapshot;
    if (m_ready)
        emit snapshotChanged(m_snapshot);
}
