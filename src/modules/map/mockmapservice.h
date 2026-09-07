#pragma once

#include "modules/map/imapservice.h"

#include <QHash>
#include <QStringList>

#include <functional>

/**
 * 可配置地图服务替身。fixture 注入的坐标必须已经规范化为 GCJ-02；实现负责输入、
 * 输出、请求终态、超时与取消约束，不包含地图 Key 或供应商协议。
 */
class MockMapService final : public IMapService
{
    Q_OBJECT

public:
    enum class Outcome
    {
        Success,
        Failure,
        NoResponse
    };
    Q_ENUM(Outcome)

    struct Behavior
    {
        Outcome outcome = Outcome::Success;
        int responseDelayMs = 0;
        int timeoutMs = 5000;
        ClientError error;
    };

    explicit MockMapService(QObject *parent = nullptr);

    void setLocationResult(const LocationResult &result);
    void clearLocationResult();
    void setGeocodeResult(const QString &address, const GeocodeResult &result);
    void setRouteResult(const QString &stationId,
                        TravelMode mode,
                        const RouteResult &result);
    void clearRouteResult(const QString &stationId, TravelMode mode);
    void setLocateBehavior(const Behavior &behavior);
    void setGeocodeBehavior(const Behavior &behavior);
    void setRouteBehavior(const Behavior &behavior);

    int locateRequestCount() const;
    int geocodeRequestCount() const;
    int routeRequestCount() const;
    QStringList cancelledRequestIds() const;

    void locate(const RequestContext &context) override;
    void geocode(const RequestContext &context,
                 const QString &address) override;
    void planRoute(const RequestContext &context,
                   const RouteQuery &query) override;
    void cancel(const QString &requestId) override;

private:
    using Completion = std::function<void()>;

    bool validateContext(const RequestContext &context);
    void startRequest(const RequestContext &context,
                      const Behavior &behavior,
                      Completion success);
    void finishFailure(const QString &requestId,
                       quint64 serial,
                       const ClientError &configuredError,
                       const QString &fallbackCode,
                       const QString &fallbackMessage,
                       bool fallbackRetryable);
    bool takePending(const QString &requestId, quint64 serial);
    void failLocal(const RequestContext &context,
                   const QString &code,
                   const QString &message,
                   bool retryable = false);
    bool validateLocationResult(const LocationResult &result) const;
    bool validateGeocodeResult(const GeocodeResult &result) const;
    bool validateRouteResult(const RouteResult &result) const;
    static QString normalizedAddress(const QString &address);
    static QString routeKey(const QString &stationId, TravelMode mode);

    std::optional<LocationResult> m_locationResult;
    QHash<QString, GeocodeResult> m_geocodeResults;
    QHash<QString, RouteResult> m_routeResults;
    Behavior m_locateBehavior;
    Behavior m_geocodeBehavior;
    Behavior m_routeBehavior;
    QHash<QString, quint64> m_pendingRequests;
    QStringList m_cancelledRequestIds;
    quint64 m_nextSerial = 0;
    int m_locateRequestCount = 0;
    int m_geocodeRequestCount = 0;
    int m_routeRequestCount = 0;
};
