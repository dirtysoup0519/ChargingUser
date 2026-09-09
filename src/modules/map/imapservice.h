#pragma once

#include "common/clienterror.h"
#include "common/requestcontext.h"
#include "modules/map/maptypes.h"

#include <QObject>
#include <QString>

/**
 * 地图供应商能力的领域边界。调用方生成非空 requestId；这些调用均为只读操作，
 * operationId 必须为空。实现负责超时、错误规范化和尽力取消。
 */
class IMapService : public QObject
{
    Q_OBJECT

public:
    explicit IMapService(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~IMapService() override = default;

public slots:
    virtual void locate(const RequestContext &context) = 0;
    virtual void geocode(const RequestContext &context,
                         const QString &address) = 0;
    virtual void planRoute(const RequestContext &context,
                           const RouteQuery &query) = 0;
    virtual void cancel(const QString &requestId) = 0;

signals:
    void locationReady(const RequestContext &context,
                       const LocationResult &result);
    void geocodeReady(const RequestContext &context,
                      const GeocodeResult &result);
    void routeReady(const RequestContext &context,
                    const RouteResult &result);
    void requestFailed(const ClientError &error);
};
