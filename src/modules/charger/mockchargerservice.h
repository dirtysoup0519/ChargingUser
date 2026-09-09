#pragma once

#include "modules/charger/ichargerservice.h"

#include <QHash>
#include <QStringList>

#include <functional>

/**
 * 可配置的站点服务替身。正式站点协议尚未冻结，因此该实现只用于测试和演示装配；
 * 数据必须由 fixture 注入，不包含正式业务默认值。
 */
class MockChargerService final : public IChargerService
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

    explicit MockChargerService(QObject *parent = nullptr);

    void setStationCatalog(const QVector<StationDetail> &stations);
    void setStationsBehavior(const Behavior &behavior);
    void setStationDetailBehavior(const Behavior &behavior);

    int stationsRequestCount() const;
    int stationDetailRequestCount() const;
    QStringList cancelledRequestIds() const;

    void queryStations(const RequestContext &context,
                       const StationQuery &query) override;
    void queryStationDetail(const RequestContext &context,
                            const QString &stationId) override;
    void cancel(const QString &requestId) override;
    void applyConfirmedChargerStatus(const QString &stationId,
                                     const QString &chargerId,
                                     ChargerBusinessStatus status) override;

private:
    using Completion = std::function<void()>;

    bool validateContext(const RequestContext &context);
    bool validateStationQuery(const RequestContext &context,
                              const StationQuery &query);
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
    StationPage buildPage(const StationQuery &query) const;

    QVector<StationDetail> m_stationCatalog;
    Behavior m_stationsBehavior;
    Behavior m_stationDetailBehavior;
    QHash<QString, quint64> m_pendingRequests;
    QStringList m_cancelledRequestIds;
    quint64 m_nextSerial = 0;
    int m_stationsRequestCount = 0;
    int m_stationDetailRequestCount = 0;
};
