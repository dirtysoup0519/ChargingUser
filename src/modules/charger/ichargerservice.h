#pragma once

#include "common/clienterror.h"
#include "common/requestcontext.h"
#include "modules/charger/chargertypes.h"

#include <QObject>
#include <QString>

/**
 * 充电站与电桩查询的领域边界。服务端数据是站点、电桩状态及业务权限的权威来源；
 * 地图供应商结果不得覆盖这些字段。
 */
class IChargerService : public QObject
{
    Q_OBJECT

public:
    explicit IChargerService(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~IChargerService() override = default;

public slots:
    virtual void queryStations(const RequestContext &context,
                               const StationQuery &query) = 0;
    virtual void queryStationDetail(const RequestContext &context,
                                    const QString &stationId) = 0;
    virtual void cancel(const QString &requestId) = 0;

signals:
    void stationsReady(const RequestContext &context,
                       const StationPage &page);
    void stationDetailReady(const RequestContext &context,
                            const StationDetail &detail);
    void requestFailed(const ClientError &error);
};
