#include "realchargerservice.h"

#include "backendclient.h"
#include "protocol.h"

#include <QTimer>

#include <algorithm>

namespace {

constexpr int kMaximumPageSize = 100;

/** 与 Mock::isInside 相同的视野判断：首版不支持跨国际日期变更线。 */
bool isInside(const GeoPoint &point, const GeoBounds &bounds)
{
    return point.latitude >= bounds.southWest.latitude
           && point.latitude <= bounds.northEast.latitude
           && point.longitude >= bounds.southWest.longitude
           && point.longitude <= bounds.northEast.longitude;
}

/** 服务器错误码中可重试的类型（数据库瞬态类）；其余按不可重试处理。 */
bool isRetryableServerError(int errType)
{
    return errType == DB_ERROR;
}

QString recordString(const QJsonObject &record, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const QJsonValue value = record.value(QLatin1String(key));
        if (value.isString()) {
            return value.toString().trimmed();
        }
    }
    return QString();
}

double numberValue(const QJsonValue &value, bool *ok)
{
    *ok = false;
    if (value.isDouble()) {
        *ok = true;
        return value.toDouble();
    }
    if (value.isString()) {
        bool parsed = false;
        const double number = value.toString().toDouble(&parsed);
        if (parsed) {
            *ok = true;
            return number;
        }
    }
    return 0.0;
}

} // namespace

RealChargerService::RealChargerService(BackendClient *backend, QObject *parent)
    : IChargerService(parent)
    , m_backend(backend)
{
    // 连接断开时立即失败全部在途查询（可重试），不等超时兜底：
    // BackendClient 会自动重连，快速失败让上层重试路径更可预期。
    connect(m_backend, &BackendClient::frameReceived,
            this, &RealChargerService::handleFrame);
    connect(m_backend, &BackendClient::connectionStateChanged,
            this, &RealChargerService::handleConnectionStateChanged);
}

void RealChargerService::setRequestTimeoutMs(int timeoutMs)
{
    m_requestTimeoutMs = timeoutMs;
}

void RealChargerService::queryStations(const RequestContext &context,
                                       const StationQuery &query)
{
    if (!context.isValid()) {
        emitFailed(context, QStringLiteral("charger-invalid-request"),
                   QStringLiteral("A request ID is required."), false);
        return;
    }
    if (context.isMutation()) {
        emitFailed(context, QStringLiteral("charger-readonly-operation"),
                   QStringLiteral("Station queries must not contain an operation ID."),
                   false);
        return;
    }
    // 与 Mock 相同的校验语义：未定位时允许默认目录浏览，center/bounds 互斥。
    if (!query.hasAtMostOneArea()
        || (query.center && !query.center->isValid())
        || (query.bounds && !query.bounds->isValid())) {
        emitFailed(context, QStringLiteral("charger-invalid-area"),
                   QStringLiteral("At most one valid search area is allowed."), false);
        return;
    }
    if (query.pageSize < 1 || query.pageSize > kMaximumPageSize) {
        emitFailed(context, QStringLiteral("charger-invalid-page-size"),
                   QStringLiteral("Page size must be between 1 and 100."), false);
        return;
    }

    startQuery(QueryKind::StationList, context, query, QString());
}

void RealChargerService::queryStationDetail(const RequestContext &context,
                                            const QString &stationId)
{
    if (!context.isValid()) {
        emitFailed(context, QStringLiteral("charger-invalid-request"),
                   QStringLiteral("A request ID is required."), false);
        return;
    }
    if (context.isMutation()) {
        emitFailed(context, QStringLiteral("charger-readonly-operation"),
                   QStringLiteral("Station queries must not contain an operation ID."),
                   false);
        return;
    }
    if (stationId.trimmed().isEmpty()) {
        emitFailed(context, QStringLiteral("charger-invalid-station-id"),
                   QStringLiteral("Station ID is required."), false);
        return;
    }

    startQuery(QueryKind::StationDetail, context, StationQuery{}, stationId.trimmed());
}

void RealChargerService::cancel(const QString &requestId)
{
    // 尽力取消：摘除在途关联并停表；此后到达的应答因查不到关联被静默丢弃。
    if (m_pending && m_pending->requestId == requestId) {
        if (m_pending->timer) {
            m_pending->timer->stop();
            m_pending->timer->deleteLater();
        }
        m_pending.reset();
    }
}

void RealChargerService::applyConfirmedChargerStatus(
    const QString &stationId, const QString &chargerId,
    ChargerBusinessStatus status)
{
    Q_UNUSED(stationId)
    Q_UNUSED(chargerId)
    Q_UNUSED(status)
    // 正式环境的预约/充电服务已经在服务端完成持久化；下次 119/229 查询负责校准。
}

bool RealChargerService::startQuery(QueryKind kind, const RequestContext &context,
                                    const StationQuery &query, const QString &stationId)
{
    // 229 暂无可靠 requestId 回显，同类查询保持单在途。
    if (m_pending) {
        emitFailed(context, QStringLiteral("charger-request-in-flight"),
                   QStringLiteral("Another station query is still in progress."),
                   true);
        return false;
    }
    if (m_backend->connectionState() != ConnectionState::Connected) {
        emitFailed(context, QStringLiteral("not-connected"),
                   QStringLiteral("Server connection is not established."), true);
        return false;
    }

    PendingRequest pending;
    pending.kind = kind;
    pending.requestId = context.requestId;
    pending.operationId = context.operationId;
    pending.stationId = stationId;
    pending.query = query;

    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, &RealChargerService::handleTimeout);
    pending.timer = timer;
    m_pending = pending;

    m_backend->sendFrame(STATION_QRY_REQ, makeStationQuery(*m_pending));
    timer->start(m_requestTimeoutMs);
    return true;
}

QJsonObject RealChargerService::makeStationQuery(const PendingRequest &pending)
{
    QJsonObject payload;
    if (pending.kind == QueryKind::StationDetail) {
        payload.insert(QStringLiteral("stationName"), pending.stationId);
    }
    // 新服务端可回显时直接获得强关联；旧 v2.6 服务端会安全忽略附加字段。
    payload.insert(QStringLiteral("requestId"), pending.requestId);
    return payload;
}

void RealChargerService::handleFrame(int msgType, const QJsonObject &payload)
{
    if (msgType >= 300 && msgType < 400) {
        // 3xx 错误应答：仅在存在在途查询时归属（v1.2 防跨类型误归属）。
        if (m_pending) {
            const ClientError error = makeBizError(msgType, payload);
            failPending(error.code, error.displayMessage, error.retryable);
        }
        return;
    }
    if (msgType != STATION_QRY_ACK || !m_pending) {
        return;
    }

    // 服务端若支持 requestId 回显，先校验归属；不回显时按 FIFO 处理。
    const QString echoed = payload.value(QStringLiteral("requestId")).toString();
    if (!echoed.isEmpty() && echoed != m_pending->requestId) {
        return;
    }

    const QJsonValue stationsValue = payload.value(QStringLiteral("stations"));
    if (!stationsValue.isArray()) {
        failPending(QStringLiteral("bad-response"),
                    QStringLiteral("Server response was malformed."), true);
        return;
    }

    m_pending->stationRecords = stationsValue.toArray();
    if (m_pending->kind == QueryKind::StationDetail) {
        const QString target = m_pending->stationId;
        const bool found = std::any_of(
            m_pending->stationRecords.cbegin(), m_pending->stationRecords.cend(),
            [&target](const QJsonValue &value) {
                return value.isObject()
                       && stationField(value.toObject()) == target;
            });
        if (!found) {
            failPending(QStringLiteral("charger-station-not-found"),
                        QStringLiteral("Requested station does not exist."), false);
            return;
        }
    }
    for (const QJsonValue &stationValue : std::as_const(m_pending->stationRecords)) {
        if (!stationValue.isObject()) {
            continue;
        }
        const QJsonArray chargers =
            stationValue.toObject().value(QStringLiteral("chargers")).toArray();
        for (const QJsonValue &chargerValue : chargers) {
            m_pending->chargerRecords.append(chargerValue);
        }
    }
    finishQuery();
}

void RealChargerService::finishQuery()
{
    // 先拷贝再摘除：发布函数只使用拷贝，避免引用悬空。
    const PendingRequest pending = *m_pending;
    m_pending.reset();
    if (pending.timer) {
        pending.timer->stop();
        pending.timer->deleteLater();
    }

    if (pending.kind == QueryKind::StationList) {
        publishPage(pending);
    } else {
        publishDetail(pending);
    }
}

void RealChargerService::publishPage(const PendingRequest &pending)
{
    const RequestContext context{pending.requestId, pending.operationId};

    // 解析两表记录：坏行跳过并计数，不让单条脏数据拖垮整个列表。
    QHash<QString, QJsonObject> stations;
    for (const QJsonValue &value : std::as_const(pending.stationRecords)) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject record = value.toObject();
        const QString stationId = stationField(record);
        if (stationId.isEmpty()) {
            continue;
        }
        stations.insert(stationId, record);
    }

    // 按 stationName 归组电桩：总数与可用数（在线且空闲）都来自 charger 表。
    QHash<QString, QVector<QJsonObject>> chargersByStation;
    for (const QJsonValue &value : std::as_const(pending.chargerRecords)) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject record = value.toObject();
        const QString owner = chargerStationField(record);
        if (owner.isEmpty() || !stations.contains(owner)) {
            continue;
        }
        chargersByStation[owner].append(record);
    }

    // 过滤与分页语义与 Mock::buildPage 保持一致：关键词匹配名称/地址；
    // 缺坐标的站点不过滤（服务端可能返回无坐标但可看文字详情的站点）。
    // 顺序必须确定：按 station 表应答顺序遍历（QHash 迭代顺序不定，
    // 会造成两次相同查询返回不同页序，游标翻页随之错乱）。
    const QString keyword = pending.query.keyword.trimmed();
    QVector<StationSummary> matches;
    for (const QJsonValue &value : std::as_const(pending.stationRecords)) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject record = value.toObject();
        StationSummary station;
        station.stationId = stationField(record);
        if (station.stationId.isEmpty()) {
            continue;
        }
        station.name = station.stationId;
        station.address = recordString(record, {"address", "addr"});
        station.point = parsePoint(record);
        station.priceCentsPerKwh = parsePriceCents(record);

        if (!keyword.isEmpty() && !station.name.contains(keyword, Qt::CaseInsensitive)
            && !station.address.contains(keyword, Qt::CaseInsensitive)) {
            continue;
        }
        if (pending.query.bounds && station.point
            && !isInside(*station.point, *pending.query.bounds)) {
            continue;
        }

        const QVector<QJsonObject> &chargers = chargersByStation.value(station.stationId);
        station.totalCount = chargers.size();
        int available = 0;
        for (const QJsonObject &charger : chargers) {
            const bool online = parseOnline(charger);
            const ChargerBusinessStatus status = parseBusinessStatus(charger);
            if (online && status == ChargerBusinessStatus::Idle) {
                ++available;
            }
        }
        station.availableCount = available;
        matches.append(station);
    }

    const int offset = pending.query.cursor.isEmpty()
                           ? 0
                           : pending.query.cursor.toInt();
    if (offset < 0 || (offset > 0 && offset >= matches.size())) {
        // 游标越界按无效输入处理，与 Mock 的 charger-invalid-cursor 语义一致。
        emitFailed(context, QStringLiteral("charger-invalid-cursor"),
                   QStringLiteral("Pagination cursor is out of range."), false);
        return;
    }
    const int remaining = std::max(0, static_cast<int>(matches.size()) - offset);
    const int count = std::min(std::max(1, pending.query.pageSize), remaining);

    StationPage page;
    page.items.reserve(count);
    for (int index = 0; index < count; ++index) {
        page.items.append(matches.at(offset + index));
    }
    const int nextOffset = offset + count;
    page.hasMore = nextOffset < matches.size();
    if (page.hasMore) {
        page.nextCursor = QString::number(nextOffset);
    }
    emit stationsReady(context, page);
}

void RealChargerService::publishDetail(const PendingRequest &pending)
{
    const RequestContext context{pending.requestId, pending.operationId};

    // 专用查询在站点不存在时通常返回 300；仍防御性处理空数组。
    const QString target = pending.stationId;
    QJsonObject stationRecord;
    for (const QJsonValue &value : std::as_const(pending.stationRecords)) {
        if (value.isObject() && stationField(value.toObject()) == target) {
            stationRecord = value.toObject();
            break;
        }
    }
    if (stationRecord.isEmpty()) {
        emitFailed(context, QStringLiteral("charger-station-not-found"),
                   QStringLiteral("Requested station does not exist."), false);
        return;
    }

    StationDetail detail;
    detail.stationId = target;
    detail.summary.stationId = target;
    detail.summary.name = target;
    detail.summary.address = recordString(stationRecord, {"address", "addr"});
    detail.summary.point = parsePoint(stationRecord);
    detail.summary.priceCentsPerKwh = parsePriceCents(stationRecord);
    detail.updatedAtUtc = QDateTime::currentDateTimeUtc();

    // 229 内嵌电桩仍按 stationName 防御性过滤，避免脏数据串入其他站点。
    int available = 0;
    for (const QJsonValue &value : std::as_const(pending.chargerRecords)) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject record = value.toObject();
        if (chargerStationField(record) != target) {
            continue;
        }

        ChargerSummary charger;
        charger.chargerId = chargerField(record);
        if (charger.chargerId.isEmpty()) {
            continue; // 缺权威键的电桩记录不可用，跳过不让其污染列表
        }
        charger.type = recordString(record, {"type"});
        bool powerOk = false;
        const double power = numberValue(record.value(QStringLiteral("powerKw")), &powerOk);
        if (powerOk) {
            charger.powerKw = power;
        } else {
            const double legacy = numberValue(record.value(QStringLiteral("power")), &powerOk);
            if (powerOk) {
                charger.powerKw = legacy;
            }
        }
        charger.online = parseOnline(record);
        charger.businessStatus = parseBusinessStatus(record);
        charger.canStartCharging = charger.online
                                   && charger.businessStatus == ChargerBusinessStatus::Idle;
        charger.disabledReason = charger.canStartCharging
                                     ? QString()
                                     : disabledReasonFor(charger.online,
                                                         charger.businessStatus);
        if (charger.canStartCharging) {
            ++available;
        }
        detail.chargers.append(charger);
    }
    detail.summary.totalCount = detail.chargers.size();
    detail.summary.availableCount = available;

    // 空电桩列表是合法详情（0 桩站点）；不在这里伪造任何权限或价格。
    emit stationDetailReady(context, detail);
}

void RealChargerService::handleConnectionStateChanged(ConnectionState state)
{
    // 断线立即失败全部在途查询并允许重试：BackendClient 自动重连时会先进入
    // Reconnecting（handleDisconnected 在 m_started 后走该分支），两种状态
    // 都意味着当前应答不可能再到达，比等超时兜底更快给出可恢复界面。
    if ((state == ConnectionState::Disconnected
         || state == ConnectionState::Reconnecting)
        && m_pending) {
        failAllPending(QStringLiteral("connection-lost"),
                       QStringLiteral("Connection to server was lost."));
    }
}

void RealChargerService::handleTimeout()
{
    if (!m_pending) {
        return; // 迟到的定时器触发：查询已被完成或取消
    }
    failPending(QStringLiteral("request-timeout"),
                QStringLiteral("Request timed out."), true);
}

void RealChargerService::failPending(const QString &code, const QString &message,
                                     bool retryable)
{
    if (!m_pending) {
        return;
    }
    const PendingRequest pending = *m_pending;
    m_pending.reset();
    if (pending.timer) {
        pending.timer->stop();
        pending.timer->deleteLater();
    }
    emit requestFailed(makeError(pending.requestId, pending.operationId,
                                 code, message, retryable));
}

void RealChargerService::failAllPending(const QString &code, const QString &message)
{
    // 当前全局单在途，循环仅为语义完整保留；未来放开并发时直接覆盖多请求。
    while (m_pending) {
        failPending(code, message, true);
    }
}

void RealChargerService::emitFailed(const RequestContext &context, const QString &code,
                                    const QString &message, bool retryable)
{
    emit requestFailed(makeError(context.requestId, context.operationId,
                                 code, message, retryable));
}

QString RealChargerService::stationField(const QJsonObject &record)
{
    // stationName 是协议层权威键（DEV_ONLINE/START_CHARGING 均使用）；
    // 兼容 "name" 别名，联调校准后收敛。
    return recordString(record, {"stationName", "name"});
}

QString RealChargerService::chargerField(const QJsonObject &record)
{
    return recordString(record, {"chargerCode", "chargerId"});
}

QString RealChargerService::chargerStationField(const QJsonObject &record)
{
    return recordString(record, {"stationName"});
}

std::optional<GeoPoint> RealChargerService::parsePoint(const QJsonObject &record)
{
    bool lngOk = false;
    double lng = numberValue(record.value(QStringLiteral("longitude")), &lngOk);
    if (!lngOk) {
        lng = numberValue(record.value(QStringLiteral("lng")), &lngOk);
        if (!lngOk) {
            return std::nullopt;
        }
    }
    bool latOk = false;
    double lat = numberValue(record.value(QStringLiteral("latitude")), &latOk);
    if (!latOk) {
        lat = numberValue(record.value(QStringLiteral("lat")), &latOk);
        if (!latOk) {
            return std::nullopt;
        }
    }
    const GeoPoint point{lat, lng};
    if (!point.isValid()) {
        return std::nullopt;
    }
    return point;
}

std::optional<qint64> RealChargerService::parsePriceCents(const QJsonObject &record)
{
    const QJsonValue cents = record.value(QStringLiteral("priceCents"));
    if (cents.isDouble()) {
        return static_cast<qint64>(cents.toDouble());
    }
    // 兼容"元"字段：协议权威存储是分（STATION_DEFAULT_PRICE_CENTS 语义），
    // 元转分必须四舍五入，避免浮点截断造成 149.9999 分。
    bool ok = false;
    const double yuan = numberValue(record.value(QStringLiteral("price")), &ok);
    if (ok && yuan >= 0.0) {
        return static_cast<qint64>(qRound64(yuan * 100.0));
    }
    return std::nullopt;
}

ChargerBusinessStatus RealChargerService::parseBusinessStatus(const QJsonObject &record)
{
    bool ok = false;
    const double raw = numberValue(record.value(QStringLiteral("businessStatus")), &ok);
    if (!ok) {
        return ChargerBusinessStatus::Unknown;
    }
    switch (static_cast<int>(raw)) {
    case CHARGER_IDLE: return ChargerBusinessStatus::Idle;
    case CHARGER_RESERVED: return ChargerBusinessStatus::Reserved;
    case CHARGER_CHARGING: return ChargerBusinessStatus::Charging;
    case CHARGER_FAULT: return ChargerBusinessStatus::Fault;
    case CHARGER_RESTARTING: return ChargerBusinessStatus::Restarting;
    default: return ChargerBusinessStatus::Unknown;
    }
}

bool RealChargerService::parseOnline(const QJsonObject &record)
{
    const QJsonValue online = record.value(QStringLiteral("online"));
    if (online.isBool()) {
        return online.toBool();
    }
    bool ok = false;
    const double raw = numberValue(online, &ok);
    return ok && static_cast<int>(raw) == 1;
}

QString RealChargerService::disabledReasonFor(bool online, ChargerBusinessStatus status)
{
    // 受控短文案：只表达协议状态本身，展示措辞由 UI 层转换。
    if (!online) {
        return QStringLiteral("离线");
    }
    switch (status) {
    case ChargerBusinessStatus::Charging: return QStringLiteral("充电中");
    case ChargerBusinessStatus::Reserved: return QStringLiteral("已预约");
    case ChargerBusinessStatus::Fault: return QStringLiteral("故障");
    case ChargerBusinessStatus::Restarting: return QStringLiteral("重启中");
    case ChargerBusinessStatus::Unknown: return QStringLiteral("状态未知");
    case ChargerBusinessStatus::Idle: break;
    }
    return QStringLiteral("暂不可用");
}

ClientError RealChargerService::makeError(const QString &requestId,
                                          const QString &operationId,
                                          const QString &code,
                                          const QString &message,
                                          bool retryable)
{
    // 只读查询永不携带 operationId / resultUnknown：合同强制约束。
    ClientError error;
    error.requestId = requestId;
    error.operationId = operationId;
    error.code = code;
    error.displayMessage = message;
    error.retryable = retryable;
    error.resultUnknown = false;
    return error;
}

ClientError RealChargerService::makeBizError(int errType, const QJsonObject &payload)
{
    // 与 RealUserNetworkApi 相同的服务端错误规范：code 字符串优先，
    // err/reason 文本仅作展示补充，不参与业务分支判断。
    const QString bizCode = payload.value(QStringLiteral("code")).toString();
    QString reason = payload.value(QStringLiteral("err")).toString();
    if (reason.isEmpty()) {
        reason = payload.value(QStringLiteral("reason")).toString();
    }
    ClientError error;
    error.code = bizCode.isEmpty()
                     ? QStringLiteral("server-error-%1").arg(errType)
                     : bizCode;
    error.displayMessage = reason.isEmpty()
                               ? QStringLiteral("Server rejected the station query (%1).")
                                     .arg(errType)
                               : reason;
    // 数据库瞬态错误可重试；参数/鉴权类错误重试无意义。
    error.retryable = isRetryableServerError(errType);
    return error;
}
