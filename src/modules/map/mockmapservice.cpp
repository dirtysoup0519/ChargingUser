#include "modules/map/mockmapservice.h"

#include <QSet>
#include <QTimer>

#include <algorithm>

MockMapService::MockMapService(QObject *parent)
    : IMapService(parent)
{
}

void MockMapService::setLocationResult(const LocationResult &result)
{
    m_locationResult = result;
}

void MockMapService::clearLocationResult()
{
    m_locationResult.reset();
}

void MockMapService::setGeocodeResult(const QString &address,
                                      const GeocodeResult &result)
{
    m_geocodeResults.insert(normalizedAddress(address), result);
}

void MockMapService::setRouteResult(const QString &stationId,
                                    TravelMode mode,
                                    const RouteResult &result)
{
    m_routeResults.insert(routeKey(stationId, mode), result);
}

void MockMapService::clearRouteResult(const QString &stationId, TravelMode mode)
{
    m_routeResults.remove(routeKey(stationId, mode));
}

void MockMapService::setLocateBehavior(const Behavior &behavior)
{
    m_locateBehavior = behavior;
}

void MockMapService::setGeocodeBehavior(const Behavior &behavior)
{
    m_geocodeBehavior = behavior;
}

void MockMapService::setRouteBehavior(const Behavior &behavior)
{
    m_routeBehavior = behavior;
}

int MockMapService::locateRequestCount() const
{
    return m_locateRequestCount;
}

int MockMapService::geocodeRequestCount() const
{
    return m_geocodeRequestCount;
}

int MockMapService::routeRequestCount() const
{
    return m_routeRequestCount;
}

QStringList MockMapService::cancelledRequestIds() const
{
    return m_cancelledRequestIds;
}

void MockMapService::locate(const RequestContext &context)
{
    ++m_locateRequestCount;
    if (!validateContext(context)) {
        return;
    }

    startRequest(context, m_locateBehavior, [this, context] {
        if (!m_locationResult) {
            failLocal(context,
                      QStringLiteral("map-location-unavailable"),
                      QStringLiteral("Current location is unavailable."));
            return;
        }
        if (!validateLocationResult(*m_locationResult)) {
            failLocal(context,
                      QStringLiteral("map-invalid-location-response"),
                      QStringLiteral("The location result is invalid."));
            return;
        }
        LocationResult result = *m_locationResult;
        result.capturedAtUtc = result.capturedAtUtc.toUTC();
        emit locationReady(context, result);
    });
}

void MockMapService::geocode(const RequestContext &context, const QString &address)
{
    ++m_geocodeRequestCount;
    if (!validateContext(context)) {
        return;
    }
    const QString key = normalizedAddress(address);
    if (key.isEmpty()) {
        failLocal(context,
                  QStringLiteral("map-empty-address"),
                  QStringLiteral("An origin address is required."));
        return;
    }

    startRequest(context, m_geocodeBehavior, [this, context, key] {
        const GeocodeResult result = m_geocodeResults.value(key);
        if (!validateGeocodeResult(result)) {
            failLocal(context,
                      QStringLiteral("map-invalid-geocode-response"),
                      QStringLiteral("The address result is invalid."));
            return;
        }
        emit geocodeReady(context, result);
    });
}

void MockMapService::planRoute(const RequestContext &context,
                               const RouteQuery &query)
{
    ++m_routeRequestCount;
    if (!validateContext(context)) {
        return;
    }
    if (query.stationId.trimmed().isEmpty()
        || !query.origin.isValid() || !query.destination.isValid()) {
        failLocal(context,
                  QStringLiteral("map-invalid-route-query"),
                  QStringLiteral("A station and valid route endpoints are required."));
        return;
    }

    startRequest(context, m_routeBehavior, [this, context, query] {
        const QString key = routeKey(query.stationId, query.mode);
        if (!m_routeResults.contains(key)) {
            // 空 polyline 是合法的“没有路线”成功结果，由 Binder 映射为 Empty。
            RouteResult empty;
            empty.stationId = query.stationId;
            empty.mode = query.mode;
            empty.origin = query.origin;
            empty.destination = query.destination;
            emit routeReady(context, empty);
            return;
        }

        RouteResult result = m_routeResults.value(key);
        if (!validateRouteResult(result)) {
            failLocal(context,
                      QStringLiteral("map-invalid-route-response"),
                      QStringLiteral("The route result is invalid."));
            return;
        }
        // 查询身份和端点来自当前权威上下文，fixture 只能提供供应商路线内容。
        result.stationId = query.stationId;
        result.mode = query.mode;
        result.origin = query.origin;
        result.destination = query.destination;
        emit routeReady(context, result);
    });
}

void MockMapService::cancel(const QString &requestId)
{
    if (requestId.isEmpty()) {
        return;
    }
    m_cancelledRequestIds.append(requestId);
    m_pendingRequests.remove(requestId);
}

bool MockMapService::validateContext(const RequestContext &context)
{
    if (!context.isValid()) {
        failLocal(context,
                  QStringLiteral("map-invalid-request"),
                  QStringLiteral("A request ID is required."));
        return false;
    }
    if (context.isMutation()) {
        failLocal(context,
                  QStringLiteral("map-readonly-operation"),
                  QStringLiteral("Map queries must not contain an operation ID."));
        return false;
    }
    return true;
}

void MockMapService::startRequest(const RequestContext &context,
                                  const Behavior &behavior,
                                  Completion success)
{
    if (m_pendingRequests.contains(context.requestId)) {
        m_pendingRequests.remove(context.requestId);
        failLocal(context,
                  QStringLiteral("map-duplicate-request-id"),
                  QStringLiteral("The request ID is already in use."));
        return;
    }

    const quint64 serial = ++m_nextSerial;
    m_pendingRequests.insert(context.requestId, serial);

    if (behavior.timeoutMs >= 0) {
        QTimer::singleShot(behavior.timeoutMs, this, [this, context, serial] {
            finishFailure(context.requestId,
                          serial,
                          ClientError{},
                          QStringLiteral("map-timeout"),
                          QStringLiteral("The map request timed out."),
                          true);
        });
    }

    if (behavior.outcome == Outcome::NoResponse) {
        return;
    }

    QTimer::singleShot(std::max(0, behavior.responseDelayMs), this,
                       [this, context, behavior, serial, success = std::move(success)] {
        if (behavior.outcome == Outcome::Failure) {
            finishFailure(context.requestId,
                          serial,
                          behavior.error,
                          QStringLiteral("map-mock-failure"),
                          QStringLiteral("The map request failed."),
                          true);
            return;
        }
        if (!takePending(context.requestId, serial)) {
            return;
        }
        success();
    });
}

void MockMapService::finishFailure(const QString &requestId,
                                   quint64 serial,
                                   const ClientError &configuredError,
                                   const QString &fallbackCode,
                                   const QString &fallbackMessage,
                                   bool fallbackRetryable)
{
    if (!takePending(requestId, serial)) {
        return;
    }
    ClientError error = configuredError;
    error.requestId = requestId;
    error.operationId.clear();
    error.resultUnknown = false;
    if (error.code.isEmpty()) {
        error.code = fallbackCode;
    }
    if (error.displayMessage.isEmpty()) {
        error.displayMessage = fallbackMessage;
    }
    if (configuredError.code.isEmpty()) {
        error.retryable = fallbackRetryable;
    }
    emit requestFailed(error);
}

bool MockMapService::takePending(const QString &requestId, quint64 serial)
{
    const auto iterator = m_pendingRequests.find(requestId);
    if (iterator == m_pendingRequests.end() || iterator.value() != serial) {
        return false;
    }
    m_pendingRequests.erase(iterator);
    return true;
}

void MockMapService::failLocal(const RequestContext &context,
                               const QString &code,
                               const QString &message,
                               bool retryable)
{
    ClientError error;
    error.requestId = context.requestId;
    error.code = code;
    error.displayMessage = message;
    error.retryable = retryable;
    emit requestFailed(error);
}

bool MockMapService::validateLocationResult(const LocationResult &result) const
{
    return result.point.isValid()
           && result.capturedAtUtc.isValid()
           && (!result.accuracyMeters
               || (std::isfinite(*result.accuracyMeters)
                   && *result.accuracyMeters >= 0.0));
}

bool MockMapService::validateGeocodeResult(const GeocodeResult &result) const
{
    QSet<QString> candidateIds;
    for (const GeocodeCandidate &candidate : result.candidates) {
        if (candidate.candidateId.isEmpty() || !candidate.point.isValid()
            || candidateIds.contains(candidate.candidateId)) {
            return false;
        }
        candidateIds.insert(candidate.candidateId);
    }
    return true;
}

bool MockMapService::validateRouteResult(const RouteResult &result) const
{
    if (result.distanceMeters < 0 || result.durationSeconds < 0) {
        return false;
    }
    if (!result.polyline.isEmpty() && result.routeId.isEmpty()) {
        return false;
    }
    for (const RouteStep &step : result.steps) {
        if (step.distanceMeters && *step.distanceMeters < 0) {
            return false;
        }
    }
    return std::all_of(result.polyline.cbegin(), result.polyline.cend(),
                       [](const GeoPoint &point) { return point.isValid(); });
}

QString MockMapService::normalizedAddress(const QString &address)
{
    return address.trimmed().toCaseFolded();
}

QString MockMapService::routeKey(const QString &stationId, TravelMode mode)
{
    return stationId + QLatin1Char('|')
           + QString::number(static_cast<int>(mode));
}
