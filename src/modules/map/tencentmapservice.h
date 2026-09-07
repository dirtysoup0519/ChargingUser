#pragma once

#include "modules/map/imapservice.h"

#include <QHash>
#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

/**
 * 腾讯地图 WebService 适配器：IMapService 的真实前端实现。
 *
 * 依据 2026-09-07《腾讯地图前端直连与 UI 接入协议》的决策：
 * - 路线规划与地理编码由 Qt 前端通过 QNetworkAccessManager 直连腾讯 WebService；
 * - 供应商坐标本身已是 GCJ-02，适配器只做校验，不做二次转换；
 * - Key 通过 setApiKey 注入，禁止写死在源码、qrc 或日志中；
 * - 定位来源（设备/IP/手动）尚未冻结，locate() 返回可区分错误而不是伪定位。
 *
 * 请求纪律与 Mock 一致：
 * - 只读查询：context 必须合法且不带 operationId，否则拒绝；
 * - 每个请求由 requestId 关联；cancel() 为尽力取消，迟到应答一律丢弃；
 * - 超时、网络失败、供应商错误、解析失败映射为可区分的 ClientError。
 */
class TencentMapService final : public IMapService
{
    Q_OBJECT

public:
    /** 生产构造：内部自建 QNetworkAccessManager 并作为子对象管理生命周期。 */
    explicit TencentMapService(QObject *parent = nullptr);
    /**
     * 测试构造：注入外部 QNetworkAccessManager 替身，适配器不取得所有权。
     * 之所以允许注入，是为了在不联网的前提下验证请求关联、取消与错误映射。
     */
    TencentMapService(QNetworkAccessManager *networkAccessManager,
                      QObject *parent = nullptr);

    ~TencentMapService() override;

    /**
     * 注入腾讯控制台申请的 WebService Key。允许运行时替换（例如登录后下发），
     * 但不允许为空调用发起请求；缺失 Key 时请求直接以 map-config-missing 失败。
     */
    void setApiKey(const QString &key);

    /**
     * 覆盖 WebService 基地址。仅测试与联调环境使用；生产不调用，
     * 默认 https://apis.map.qq.com。该地址不会写进任何日志。
     */
    void setServiceBaseUrl(const QString &baseUrl);

    /** 设置地址提示的城市范围；默认与当前首页城市一致为北京市。 */
    void setSearchRegion(const QString &region);

    /** 覆盖单个请求的传输超时（毫秒）。默认 10s：Web Service 官方建议客户端超时兜底。 */
    void setDefaultTimeoutMs(int timeoutMs);

    /** 本地学习环境可注入默认位置；未注入时 locate() 仍返回不支持。 */
    void setFallbackLocation(const std::optional<LocationResult> &location);

    void locate(const RequestContext &context) override;
    void geocode(const RequestContext &context, const QString &address) override;
    void planRoute(const RequestContext &context, const RouteQuery &query) override;
    void cancel(const QString &requestId) override;

private:
    /** 一个在途请求的全部关联信息：应答、超时定时器与类别。 */
    struct InFlight
    {
        QPointer<QNetworkReply> reply;
        QPointer<QTimer> timeoutTimer;
        QString kind; // "geocode" 或 "route"，用于选择解析分支
    };

    /** 拒绝非法请求：只读校验、重复 requestId、参数缺失等本地快速失败。 */
    void failLocal(const RequestContext &context,
                   const QString &code,
                   const QString &message,
                   bool retryable = false);

    /** 发起一个 GET 请求并挂上 requestId 关联与超时定时器；返回 false 表示未发出。 */
    bool startGet(const RequestContext &context,
                  const QString &kind,
                  const QUrl &url);

    /** 生成供应商错误映射后的 ClientError；requestId 始终回填，保证关联可追溯。 */
    ClientError mappedError(const QString &requestId,
                            const QString &code,
                            const QString &message,
                            bool retryable) const;

    void onReplyFinished(const QString &requestId);
    void onRequestTimeout(const QString &requestId);

    /** 解析地理编码（输入提示）应答并发射 geocodeReady/requestFailed。 */
    void handleGeocodePayload(const RequestContext &context,
                              const QByteArray &payload);
    /** 解析路线规划应答并发射 routeReady/requestFailed。 */
    void handleRoutePayload(const RequestContext &context,
                            const RouteQuery &query,
                            const QByteArray &payload);

    QNetworkAccessManager *m_nam = nullptr;
    bool m_ownedNam = false;
    QString m_apiKey;
    QString m_baseUrl = QStringLiteral("https://apis.map.qq.com");
    QString m_searchRegion = QStringLiteral("北京市");
    int m_defaultTimeoutMs = 10000;
    std::optional<LocationResult> m_fallbackLocation;

    /** requestId → 在途请求。迟到应答以"查不到关联"被识别并丢弃。 */
    QHash<QString, InFlight> m_pending;
};
