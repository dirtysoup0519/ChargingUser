#pragma once

#include "common/clienterror.h"
#include "common/connectionstate.h"
#include "common/requestcontext.h"
#include "modules/charger/ichargerservice.h"

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>

#include <optional>

class BackendClient;
class QTimer;

/**
 * IChargerService 的真实适配器（服务端协议 v2.6，站点专用查询通道）。
 *
 * 协议现实约束：
 * - 站点数据走 `119 STATION_QRY_REQ` → `229 STATION_QRY_ACK`，每个站点
 *   内嵌 chargers，避免与订单、钱包共用 `200 DATA` 造成跨适配器误归属；
 * - 服务端暂未稳定回显 requestId，因此同类查询仍保持单在途；
 * - 协议无 requestId 回传约定：请求按合同 §11 v1.1/v1.2 保守规则关联，
 *   载荷仍携带 requestId 以便服务端未来支持回显时自动升级。
 *
 * 字段映射（服务端权威键）：
 * - stationId = station 表 stationName 字段；chargerId = chargerCode 字段；
 * - 坐标/价格/状态字段做宽容解析（数值或数字字符串均可），非法记录跳过，
 *   不让单条脏数据拖垮整个列表；
 * - canStartCharging = online && businessStatus==Idle，直接表达协议
 *   CHARGER_* 枚举语义，不推断服务端未表达的任何权限。
 *
 * 请求纪律与 Mock 一致：只读查询禁止 operationId；cancel 为尽力取消，
 * 迟到应答因在途关联已摘除而被静默丢弃。
 */
class RealChargerService final : public IChargerService
{
    Q_OBJECT

public:
    explicit RealChargerService(BackendClient *backend, QObject *parent = nullptr);

    /** 供测试与部署调节；默认 10s，覆盖两个表的两步往返。 */
    void setRequestTimeoutMs(int timeoutMs);

public slots:
    void queryStations(const RequestContext &context,
                       const StationQuery &query) override;
    void queryStationDetail(const RequestContext &context,
                            const QString &stationId) override;
    void cancel(const QString &requestId) override;

private slots:
    void handleFrame(int msgType, const QJsonObject &payload);
    void handleConnectionStateChanged(ConnectionState state);

private:
    enum class QueryKind
    {
        StationList,
        StationDetail
    };

    struct PendingRequest
    {
        QueryKind kind = QueryKind::StationList;
        QString requestId;
        QString operationId;
        QString stationId;      // 仅 Detail 用：目标站点（= stationName）
        StationQuery query;     // 仅 List 用：过滤与分页参数
        QJsonArray stationRecords;
        QJsonArray chargerRecords;
        int skippedRecords = 0; // 宽容解析跳过的脏行数（诊断用）
        QTimer *timer = nullptr;
    };

    bool startQuery(QueryKind kind, const RequestContext &context,
                    const StationQuery &query, const QString &stationId);
    void finishQuery();
    void failPending(const QString &code, const QString &message, bool retryable);
    void failAllPending(const QString &code, const QString &message);
    void emitFailed(const RequestContext &context, const QString &code,
                    const QString &message, bool retryable);

    void publishPage(const PendingRequest &pending);
    void publishDetail(const PendingRequest &pending);
    void handleTimeout();

    static QJsonObject makeStationQuery(const PendingRequest &pending);
    static QString stationField(const QJsonObject &record);
    static std::optional<GeoPoint> parsePoint(const QJsonObject &record);
    static std::optional<qint64> parsePriceCents(const QJsonObject &record);
    static ChargerBusinessStatus parseBusinessStatus(const QJsonObject &record);
    static bool parseOnline(const QJsonObject &record);
    static QString chargerField(const QJsonObject &record);
    static QString chargerStationField(const QJsonObject &record);
    static QString disabledReasonFor(bool online, ChargerBusinessStatus status);
    static ClientError makeError(const QString &requestId,
                                 const QString &operationId,
                                 const QString &code,
                                 const QString &message,
                                 bool retryable);
    static ClientError makeBizError(int errType, const QJsonObject &payload);

    BackendClient *m_backend;
    int m_requestTimeoutMs = 10000;
    /** 同类单在途查询：229 当前没有可靠 requestId 回显。 */
    std::optional<PendingRequest> m_pending;
};
