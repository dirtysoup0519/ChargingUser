#include "modules/map/tencentmapservice.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>
#include <QUrlQuery>

namespace {

/**
 * 腾讯方向服务返回的 polyline 是增量压缩字符串：每个坐标对按"纬度、经度"顺序，
 * 以 5 位一组变长编码并基于前一个点累加，最终除以 1e6 得到度。
 * 实现按腾讯官方 demo 的解码规则移植；入口非法（长度越界）时返回空列表，
 * 由上层把空 polyline 统一映射为"暂无路线"，不制造半截数据。
 */
qint32 decodePolylineDelta(const QVector<uint> &codes, int &index, bool &ok)
{
    // 官方规则：字符码 - 63 - 1 得到原始分组，低位 5 位是数据，0x1f 是结束标志。
    qint32 result = 0;
    int shift = 0;
    qint32 chunk = 0;
    do {
        if (index >= codes.size()) {
            ok = false;
            return 0;
        }
        chunk = static_cast<qint32>(codes.at(index++)) - 63 - 1;
        result += (chunk & 0x1f) << shift;
        shift += 5;
    } while (chunk >= 0x1f);
    // 最高位是符号：1 表示负增量（ zigzag 变体），否则为正增量。
    return (result & 1) ? ~(result >> 1) : (result >> 1);
}

QVector<GeoPoint> decodeTencentPolyline(const QString &text)
{
    QVector<GeoPoint> points;
    if (text.isEmpty()) {
        return points;
    }
    const QVector<uint> codes = text.toUcs4();
    int index = 0;
    double lat = 0.0;
    double lng = 0.0;
    while (index < codes.size()) {
        bool ok = true;
        const qint32 deltaLat = decodePolylineDelta(codes, index, ok);
        if (!ok) {
            // 剩余字节不足：供应商数据被截断，宁可返回已解码前缀之外的空结果，
            // 也不拼出偏移的错误坐标交给地图绘制。
            return {};
        }
        const qint32 deltaLng = decodePolylineDelta(codes, index, ok);
        if (!ok) {
            return {};
        }
        lat += deltaLat;
        lng += deltaLng;
        const GeoPoint point{lat / 1e6, lng / 1e6};
        if (!point.isValid()) {
            // 解出的坐标超出合法范围说明编码假设被破坏，直接失败而不是绘制错线。
            return {};
        }
        points.append(point);
    }
    return points;
}

/** 去掉供应商文本中混入的 HTML 标签（如 <b>），保持 DTO 文案是纯文本。 */
QString stripHtmlTags(const QString &text)
{
    static const QRegularExpression tagPattern(QStringLiteral("<[^>]*>"));
    QString plain = text;
    plain.remove(tagPattern);
    return plain.trimmed();
}

QJsonObject firstRouteObject(const QJsonDocument &document, bool &ok)
{
    ok = false;
    if (!document.isObject()) {
        return {};
    }
    const QJsonValue resultValue = document.object().value(QStringLiteral("result"));
    if (!resultValue.isObject()) {
        return {};
    }
    const QJsonValue routesValue = resultValue.toObject().value(QStringLiteral("routes"));
    if (!routesValue.isArray()) {
        return {};
    }
    const QJsonArray routes = routesValue.toArray();
    ok = true; // 数组存在但可能为空：空表示"暂无可用路线"，属于成功应答。
    if (routes.isEmpty()) {
        return {};
    }
    return routes.first().toObject();
}

} // namespace

TencentMapService::TencentMapService(QObject *parent)
    : TencentMapService(nullptr, parent)
{
    // 标记为自建实例：析构时需要负责销毁内部 QNetworkAccessManager。
    m_ownedNam = true;
    m_nam = new QNetworkAccessManager(this);
}

TencentMapService::TencentMapService(QNetworkAccessManager *networkAccessManager,
                                     QObject *parent)
    : IMapService(parent)
    , m_nam(networkAccessManager)
{
}

TencentMapService::~TencentMapService() = default;

void TencentMapService::setApiKey(const QString &key)
{
    m_apiKey = key.trimmed();
}

void TencentMapService::setServiceBaseUrl(const QString &baseUrl)
{
    m_baseUrl = baseUrl;
}

void TencentMapService::setDefaultTimeoutMs(int timeoutMs)
{
    m_defaultTimeoutMs = timeoutMs;
}

void TencentMapService::locate(const RequestContext &context)
{
    // 定位来源尚未冻结（BitDev 虚拟机无设备定位，手动地址是首版正式路径）。
    // 这里返回可区分错误而不是伪造定位，保证 UI 显示"定位不可用"而不是假当前位置。
    failLocal(context,
              QStringLiteral("map-locate-unsupported"),
              QStringLiteral("定位方式尚未启用，请使用手动输入起点"),
              /*retryable=*/false);
}

void TencentMapService::geocode(const RequestContext &context,
                                const QString &address)
{
    if (!context.isValid() || context.isMutation()) {
        failLocal(context,
                  QStringLiteral("map-readonly-operation"),
                  QStringLiteral("地理编码是只读查询，不允许携带 operationId"));
        return;
    }
    const QString keyword = address.trimmed();
    if (keyword.isEmpty()) {
        failLocal(context,
                  QStringLiteral("map-invalid-input"),
                  QStringLiteral("地址不能为空"));
        return;
    }
    if (m_apiKey.isEmpty()) {
        failLocal(context,
                  QStringLiteral("map-key-missing"),
                  QStringLiteral("地图服务未配置，暂时无法解析地址"));
        return;
    }

    // 输入提示接口：一次返回多个候选（id/title/address/location），
    // 与 GeocodeCandidate.candidateId 语义一一对应，避免自造候选 ID。
    QUrlQuery parameters;
    parameters.addQueryItem(QStringLiteral("key"), m_apiKey);
    parameters.addQueryItem(QStringLiteral("keyword"), keyword);
    QUrl url(m_baseUrl + QStringLiteral("/ws/place/v1/suggestion"));
    url.setQuery(parameters);
    startGet(context, QStringLiteral("geocode"), url);
}

void TencentMapService::planRoute(const RequestContext &context,
                                  const RouteQuery &query)
{
    if (!context.isValid() || context.isMutation()) {
        failLocal(context,
                  QStringLiteral("map-readonly-operation"),
                  QStringLiteral("路线规划是只读查询，不允许携带 operationId"));
        return;
    }
    if (!query.origin.isValid() || !query.destination.isValid()
        || query.stationId.isEmpty()) {
        failLocal(context,
                  QStringLiteral("map-invalid-input"),
                  QStringLiteral("路线起终点或站点标识不完整"));
        return;
    }
    if (m_apiKey.isEmpty()) {
        failLocal(context,
                  QStringLiteral("map-key-missing"),
                  QStringLiteral("地图服务未配置，暂时无法规划路线"));
        return;
    }

    // 路线按出行方式走不同端点：driving 与 walking 是两个独立 WebService API。
    const QString modePath = (query.mode == TravelMode::Walking)
                                 ? QStringLiteral("walking")
                                 : QStringLiteral("driving");
    QUrlQuery parameters;
    parameters.addQueryItem(QStringLiteral("key"), m_apiKey);
    parameters.addQueryItem(
        QStringLiteral("from"),
        QStringLiteral("%1,%2")
            .arg(QString::number(query.origin.latitude, 'f', 6),
                 QString::number(query.origin.longitude, 'f', 6)));
    parameters.addQueryItem(
        QStringLiteral("to"),
        QStringLiteral("%1,%2")
            .arg(QString::number(query.destination.latitude, 'f', 6),
                 QString::number(query.destination.longitude, 'f', 6)));
    QUrl url(m_baseUrl + QStringLiteral("/ws/direction/v1/%1/").arg(modePath));
    url.setQuery(parameters);
    if (!startGet(context, QStringLiteral("route"), url)) {
        return;
    }
    // 路线应答需要回填 stationId/mode/起终点，因此把整份查询挂在应答属性上，
    // finished 回调按 requestId 找回应答对象后即可还原上下文。
    if (const auto entry = m_pending.constFind(context.requestId);
        entry != m_pending.constEnd() && entry->reply) {
        entry->reply->setProperty("tencent-route-query",
                                  QVariant::fromValue(query));
    }
}

void TencentMapService::cancel(const QString &requestId)
{
    // 尽力取消：摘除在途关联后中止底层应答。abort 会触发 finished，
    // 但此时关联已移除，onReplyFinished 会按"迟到应答"直接忽略。
    const auto entry = m_pending.take(requestId);
    if (entry.reply) {
        if (entry.timeoutTimer) {
            entry.timeoutTimer->stop();
        }
        entry.reply->abort();
    }
}

bool TencentMapService::startGet(const RequestContext &context,
                                 const QString &kind,
                                 const QUrl &url)
{
    if (m_pending.contains(context.requestId)) {
        // 同一 requestId 不允许并发两跑：否则两次应答都会回填同一个 ID，无法区分。
        failLocal(context,
                  QStringLiteral("map-duplicate-request"),
                  QStringLiteral("相同请求仍在处理中"));
        return false;
    }
    if (!m_nam) {
        failLocal(context,
                  QStringLiteral("map-network"),
                  QStringLiteral("网络组件不可用"));
        return false;
    }

    QNetworkRequest request(url);
    // 允许重定向但收紧策略：WebService 只应回 HTTPS 同源应答，防降级。
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    // requestId 挂在应答对象属性上：finished 回调不捕获上下文指针，避免生命周期问题。
    request.setAttribute(
        QNetworkRequest::User,
        QVariant(context.requestId));
    QNetworkReply *reply = m_nam->get(request);
    reply->setProperty("tencent-request-id", context.requestId);
    reply->setProperty("tencent-kind", kind);

    InFlight inFlight;
    inFlight.reply = reply;
    inFlight.kind = kind;
    // 每请求独立定时器：供应商无应答时由本端兜底，保证页面一定能等到终态。
    QTimer *timeoutTimer = new QTimer(reply);
    timeoutTimer->setSingleShot(true);
    const QString requestId = context.requestId;
    connect(timeoutTimer, &QTimer::timeout, this,
            [this, requestId] { onRequestTimeout(requestId); });
    timeoutTimer->start(m_defaultTimeoutMs);
    inFlight.timeoutTimer = timeoutTimer;

    connect(reply, &QNetworkReply::finished, this,
            [this, requestId] { onReplyFinished(requestId); });

    m_pending.insert(requestId, inFlight);
    return true;
}

void TencentMapService::onReplyFinished(const QString &requestId)
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        return;
    }
    reply->deleteLater();

    // 迟到/已取消的应答：关联已摘除，静默丢弃。这是"快速切站不串页"的底座。
    const auto entry = m_pending.take(requestId);
    if (!entry.reply || entry.reply.data() != reply) {
        return;
    }
    if (entry.timeoutTimer) {
        entry.timeoutTimer->stop();
    }

    // 还原请求上下文（requestId 已知；路线需要 query 里的站点与出行方式，
    // 因此把序列化的 query 也挂在属性上传递）。
    const QString kind = reply->property("tencent-kind").toString();

    if (reply->error() != QNetworkReply::NoError) {
        // 网络层失败：统一可重试（用户换网/重试即可恢复），文案不暴露供应商细节。
        const ClientError error = mappedError(
            requestId, QStringLiteral("map-network"),
            QStringLiteral("网络连接失败，请检查网络后重试"), /*retryable=*/true);
        emit requestFailed(error);
        return;
    }

    const QByteArray payload = reply->readAll();
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        const ClientError error = mappedError(
            requestId, QStringLiteral("map-parse"),
            QStringLiteral("地图服务返回了无法解析的数据"), /*retryable=*/false);
        emit requestFailed(error);
        return;
    }

    const QJsonObject root = document.object();
    const int status = root.value(QStringLiteral("status")).toInt(-1);
    const QString message = root.value(QStringLiteral("message")).toString();

    if (status == 0) {
        if (kind == QStringLiteral("geocode")) {
            handleGeocodePayload(RequestContext{requestId, {}}, payload);
        } else {
            const QVariant queryVariant = reply->property("tencent-route-query");
            const RouteQuery query = queryVariant.value<RouteQuery>();
            handleRoutePayload(RequestContext{requestId, {}}, query, payload);
        }
        return;
    }

    // 供应商错误映射：120/121 属于频率与配额限制，稍后重试有意义；
    // 其余（授权、参数、内部错误）重试大概率无效，按不可重试上报。
    if (status == 120 || status == 121) {
        const ClientError error = mappedError(
            requestId, QStringLiteral("map-rate-limited"),
            QStringLiteral("地图服务请求过于频繁，请稍后重试"), /*retryable=*/true);
        emit requestFailed(error);
        return;
    }
    const ClientError error = mappedError(
        requestId, QStringLiteral("map-provider-error"),
        QStringLiteral("地图服务返回错误：%1").arg(message), /*retryable=*/false);
    emit requestFailed(error);
}

void TencentMapService::onRequestTimeout(const QString &requestId)
{
    // 超时即终态：摘除关联并中止应答；finished 会因关联缺失被忽略。
    const auto entry = m_pending.take(requestId);
    if (!entry.reply) {
        return;
    }
    entry.reply->abort();
    const ClientError error = mappedError(
        requestId, QStringLiteral("map-timeout"),
        QStringLiteral("地图服务响应超时，请重试"), /*retryable=*/true);
    emit requestFailed(error);
}

void TencentMapService::handleGeocodePayload(const RequestContext &context,
                                             const QByteArray &payload)
{
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    const QJsonValue dataValue = document.object().value(QStringLiteral("data"));
    const QJsonArray items = dataValue.isArray() ? dataValue.toArray()
                                                 : QJsonArray();

    GeocodeResult result;
    for (const QJsonValue &itemValue : items) {
        const QJsonObject item = itemValue.toObject();
        GeocodeCandidate candidate;
        candidate.candidateId = item.value(QStringLiteral("id")).toString();
        candidate.name = item.value(QStringLiteral("title")).toString();
        candidate.fullAddress = item.value(QStringLiteral("address")).toString();
        const QJsonObject location =
            item.value(QStringLiteral("location")).toObject();
        candidate.point.latitude =
            location.value(QStringLiteral("lat")).toDouble();
        candidate.point.longitude =
            location.value(QStringLiteral("lng")).toDouble();
        // 无坐标的候选不丢弃：文档要求"缺失时不绘制虚假坐标"，保留文字供用户选择。
        if (!candidate.candidateId.isEmpty()) {
            result.candidates.append(candidate);
        }
    }
    emit geocodeReady(context, result);
}

void TencentMapService::handleRoutePayload(const RequestContext &context,
                                           const RouteQuery &query,
                                           const QByteArray &payload)
{
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    bool hasRoutesField = false;
    const QJsonObject routeObject = firstRouteObject(document, hasRoutesField);

    // 规范化结果：站标识与出行方式必须回填请求值，保证请求-应答强关联。
    RouteResult result;
    result.stationId = query.stationId;
    result.mode = query.mode;
    result.origin = query.origin;
    result.destination = query.destination;

    // routes 字段缺失或为空 = 无可用路线：按成功+空 polyline 上报，
    // 由 Binder 映射为 Empty 状态（与 Mock 行为一致），而不是错误。
    if (!hasRoutesField || routeObject.isEmpty()) {
        emit routeReady(context, result);
        return;
    }

    result.routeId = QStringLiteral("%1-%2")
                         .arg(query.stationId,
                              query.mode == TravelMode::Walking
                                  ? QStringLiteral("walking")
                                  : QStringLiteral("driving"));
    result.distanceMeters = routeObject.value(QStringLiteral("distance")).toInt(0);

    // 供应商 duration 单位是分钟；领域 DTO 统一秒，转换放在适配器（唯一知道单位处）。
    result.durationSeconds =
        routeObject.value(QStringLiteral("duration")).toInt(0) * 60;

    result.polyline =
        decodeTencentPolyline(routeObject.value(QStringLiteral("polyline")).toString());

    const QJsonValue stepsValue = routeObject.value(QStringLiteral("steps"));
    if (stepsValue.isArray()) {
        for (const QJsonValue &stepValue : stepsValue.toArray()) {
            if (stepValue.isString()) {
                // 步骤数组也可能是纯字符串（walking 端点的历史格式），防御性兼容。
                result.steps.append({stripHtmlTags(stepValue.toString()), {}});
                continue;
            }
            const QJsonObject step = stepValue.toObject();
            RouteStep stepView;
            stepView.instruction =
                stripHtmlTags(step.value(QStringLiteral("instruction")).toString());
            if (step.contains(QStringLiteral("distance"))) {
                stepView.distanceMeters =
                    step.value(QStringLiteral("distance")).toInt();
            }
            if (!stepView.instruction.isEmpty()) {
                result.steps.append(stepView);
            }
        }
    }

    emit routeReady(context, result);
}

ClientError TencentMapService::mappedError(const QString &requestId,
                                           const QString &code,
                                           const QString &message,
                                           bool retryable) const
{
    // 只读查询永不携带 operationId / resultUnknown：这是合同的强制约束。
    ClientError error;
    error.requestId = requestId;
    error.code = code;
    error.displayMessage = message;
    error.retryable = retryable;
    error.resultUnknown = false;
    return error;
}

void TencentMapService::failLocal(const RequestContext &context,
                                  const QString &code,
                                  const QString &message,
                                  bool retryable)
{
    emit requestFailed(mappedError(context.requestId, code, message, retryable));
}
