#include "realchargingnetworkapi.h"

#include "backendclient.h"
#include "protocol.h"

#include <QJsonArray>
#include <QDateTime>
#include <QTimer>

#include <cmath>
#include <initializer_list>

namespace {

bool isServerError(int msgType)
{
    return msgType >= 300 && msgType < 400;
}

QString errorMessage(const QJsonObject &payload)
{
    QString message = payload.value(QStringLiteral("err")).toString();
    if (message.isEmpty()) message = payload.value(QStringLiteral("reason")).toString();
    return message.isEmpty() ? QStringLiteral("服务器拒绝了充电请求。") : message;
}

} // namespace

RealChargingNetworkApi::RealChargingNetworkApi(BackendClient *backend,
                                               QObject *parent)
    : IChargingNetworkApi(parent), m_backend(backend)
{
    Q_ASSERT(m_backend);
    connect(m_backend, &BackendClient::frameReceived,
            this, &RealChargingNetworkApi::handleFrame);
    connect(m_backend, &BackendClient::connectionStateChanged,
            this, &RealChargingNetworkApi::handleConnectionStateChanged);
}

ChargingBackendCapabilities RealChargingNetworkApi::capabilities() const
{
    ChargingBackendCapabilities value;
    value.combinedStationChargerQuery = true;
    value.stableStationAndChargerIds = true;
    if (m_unsafeTestOperations) {
        // TEST_ONLY: training server has no operation-result query. Mutations
        // remain single-flight and unknown outcomes are never retried.
        value.responseCorrelation = true;
        value.idempotentChargingMutations = true;
        value.operationResultQuery = true;
    }
    return value;
}

void RealChargingNetworkApi::setUnsafeTestOperationsEnabled(bool enabled)
{
    m_unsafeTestOperations = enabled;
}

void RealChargingNetworkApi::setIdentity(const QString &username)
{
    const QString normalized = username.trimmed();
    if (normalized == m_username) return;
    if (m_pending) {
        failPending(QStringLiteral("identity-changed"),
                    QStringLiteral("登录账号已变化。"), false);
    }
    m_username = normalized;
}

void RealChargingNetworkApi::setRequestTimeoutMs(int timeoutMs)
{
    m_requestTimeoutMs = qMax(1, timeoutMs);
}

void RealChargingNetworkApi::loadConfirmation(const RequestContext &context,
                                              const QString &stationId,
                                              const QString &chargerId)
{
    if (chargerId.trimmed().isEmpty()
        || !begin(PendingKind::ConfirmationStation, context)) return;
    m_pending->stationId = stationId.trimmed();
    m_pending->chargerId = chargerId.trimmed();
    QJsonObject payload{{QStringLiteral("requestId"), context.requestId}};
    if (m_pending->stationId.isEmpty())
        payload.insert(QStringLiteral("chargerCode"), m_pending->chargerId);
    else
        payload.insert(QStringLiteral("stationName"), m_pending->stationId);
    if (!m_backend->sendFrame(STATION_QRY_REQ, payload)) {
        failPending(QStringLiteral("send-failed"),
                    QStringLiteral("充电确认查询发送失败。"), true);
    }
}

void RealChargingNetworkApi::startCharging(const RequestContext &context,
                                            const QString &stationId,
                                            const QString &chargerId)
{
    if (!begin(PendingKind::Start, context)) return;
    m_pending->stationId = stationId;
    m_pending->chargerId = chargerId;
    // v2.6: 服务端依据会话身份和 chargerCode 创建订单并驱动虚拟电桩；
    // stationName、requestId、operationId 不是 108 业务字段。
    Q_UNUSED(stationId);
    QJsonObject payload{{QStringLiteral("username"), m_username},
                        {QStringLiteral("chargerCode"), chargerId}};
    if (!m_backend->sendFrame(START_CHARGING_REQ, payload)) {
        failPending(QStringLiteral("send-failed"),
                    QStringLiteral("启动充电请求发送失败。"), false, true);
    }
}

void RealChargingNetworkApi::queryStartResult(const RequestContext &context,
                                              const QString &)
{
    if (!begin(PendingKind::StartRecovery, context)) return;
    QJsonObject condition{{QStringLiteral("username"), m_username}};
    if (!m_backend->sendFrame(ORDERQRY_REQ, condition))
        failPending(QStringLiteral("send-failed"), QStringLiteral("订单恢复查询发送失败。"), true);
}

void RealChargingNetworkApi::cancel(const QString &requestId)
{
    if (m_pending && m_pending->context.requestId == requestId) finishPending();
}

bool RealChargingNetworkApi::begin(PendingKind kind, const RequestContext &context)
{
    if (!context.isValid()) {
        emitFailure(context, QStringLiteral("charging-invalid-request"),
                    QStringLiteral("充电请求缺少 requestId。"));
        return false;
    }
    if (m_username.isEmpty()) {
        emitFailure(context, QStringLiteral("charging-no-identity"),
                    QStringLiteral("请先登录。"));
        return false;
    }
    if (m_backend->connectionState() != ConnectionState::Connected) {
        emitFailure(context, QStringLiteral("not-connected"),
                    QStringLiteral("服务器尚未连接。"), true);
        return false;
    }
    if (m_pending) {
        emitFailure(context, QStringLiteral("charging-request-in-flight"),
                    QStringLiteral("上一项充电请求仍在处理中。"), true);
        return false;
    }
    PendingRequest pending;
    pending.kind = kind;
    pending.context = context;
    pending.timer = new QTimer(this);
    pending.timer->setSingleShot(true);
    connect(pending.timer, &QTimer::timeout,
            this, &RealChargingNetworkApi::handleTimeout);
    m_pending = pending;
    m_pending->timer->start(m_requestTimeoutMs);
    return true;
}

bool RealChargingNetworkApi::sendTableQuery(const QString &table)
{
    QJsonObject condition{{QStringLiteral("username"), m_username}};
    return m_backend->sendFrame(
        GETDATA, QJsonObject{{QStringLiteral("table"), table},
                             {QStringLiteral("cond"), condition},
                             {QStringLiteral("requestId"),
                              m_pending->context.requestId}});
}

void RealChargingNetworkApi::handleFrame(int msgType, const QJsonObject &payload)
{
    if (!m_pending) return;
    const QString echoed = payload.value(QStringLiteral("requestId")).toString();
    if (!echoed.isEmpty() && echoed != m_pending->context.requestId) return;

    if (isServerError(msgType)) {
        failPending(payload.value(QStringLiteral("code")).toString(
                        QStringLiteral("server-error")),
                    errorMessage(payload), msgType == DB_ERROR,
                    m_pending->kind == PendingKind::Start);
        return;
    }

    if (m_pending->kind == PendingKind::ConfirmationStation
        && msgType == STATION_QRY_ACK) {
        const QJsonArray stations = payload.value(QStringLiteral("stations")).toArray();
        for (const QJsonValue &value : stations) {
            if (!value.isObject()) continue;
            const QJsonObject station = value.toObject();
            const QString responseStationId =
                stringField(station, {"stationName", "name"});
            if (!m_pending->stationId.isEmpty()
                && responseStationId != m_pending->stationId) continue;
            const QJsonArray chargers = station.value(QStringLiteral("chargers")).toArray();
            for (const QJsonValue &chargerValue : chargers) {
                if (!chargerValue.isObject()) continue;
                const QJsonObject charger = chargerValue.toObject();
                if (stringField(charger, {"chargerCode", "chargerId"})
                    == m_pending->chargerId) {
                    m_pending->stationId = responseStationId;
                    m_pending->station = station;
                    m_pending->charger = charger;
                    m_pending->kind = PendingKind::ConfirmationUser;
                    if (!sendTableQuery(QStringLiteral("user"))) {
                        failPending(QStringLiteral("send-failed"),
                                    QStringLiteral("余额查询发送失败。"), true);
                    }
                    return;
                }
            }
        }
        failPending(QStringLiteral("charger-not-found"),
                    QStringLiteral("未找到请求的站点或充电桩。"), false);
        return;
    }

    if (m_pending->kind == PendingKind::ConfirmationUser && msgType == DATA) {
        const QJsonArray rows = payload.value(QStringLiteral("data")).toArray();
        for (const QJsonValue &value : rows) {
            if (!value.isObject()) continue;
            const QJsonObject row = value.toObject();
            if (stringField(row, {"username"}) != m_username) continue;
            const auto balance = centsField(row, "balanceCents", "balance");
            if (!balance) {
                failPending(QStringLiteral("bad-response"),
                            QStringLiteral("用户余额格式错误。"), true);
                return;
            }
            m_pending->balanceCents = *balance;
            m_pending->kind = PendingKind::ConfirmationOrders;
            if (!sendTableQuery(QStringLiteral("orderInfo"))) {
                failPending(QStringLiteral("send-failed"),
                            QStringLiteral("活动订单查询发送失败。"), true);
            }
            return;
        }
        failPending(QStringLiteral("user-not-found"),
                    QStringLiteral("未找到当前登录用户。"), false);
        return;
    }

    if (m_pending->kind == PendingKind::ConfirmationOrders && msgType == DATA) {
        const QJsonArray rows = payload.value(QStringLiteral("data")).toArray();
        for (const QJsonValue &value : rows) {
            if (!value.isObject()) continue;
            const QJsonObject row = value.toObject();
            if (stringField(row, {"username"}) != m_username) continue;
            const QString status = stringField(row, {"status"});
            if (status == QLatin1String(ORDER_CHARGING)
                || status == QLatin1String(ORDER_PENDING_SETTLE)) {
                m_pending->activeOrderId = stringField(row, {"orderNo", "orderId"});
                break;
            }
        }
        const PendingRequest pending = *m_pending;
        finishPending();
        publishConfirmation(pending);
        return;
    }

    if (m_pending->kind == PendingKind::StartRecovery && msgType == ORDERQRY_ACK) {
        const QJsonArray rows = payload.value(QStringLiteral("orders")).toArray();
        const PendingRequest pending = *m_pending;
        for (const QJsonValue &value : rows) {
            if (!value.isObject()) continue;
            const QJsonObject row = value.toObject();
            if (stringField(row, {"username"}) != m_username
                || stringField(row, {"status"}) != QLatin1String(ORDER_CHARGING)) continue;
            StartChargingResult result;
            result.requestId = pending.context.requestId;
            result.operationId = pending.context.operationId;
            result.orderId = stringField(row, {"orderNo", "orderId"});
            result.stationId = stringField(row, {"stationName", "stationId"});
            result.chargerId = stringField(row, {"chargerCode", "chargerId"});
            result.priceCentsPerKwhSnapshot = centsField(row, "priceCentsSnapshot", "price").value_or(0);
            result.startedAtUtc = QDateTime::fromString(stringField(row, {"startedAt", "startTime"}), Qt::ISODate).toUTC();
            finishPending();
            emit chargingStarted(pending.context, result);
            return;
        }
        failPending(QStringLiteral("order-not-found"), QStringLiteral("未找到正在充电的订单，请刷新订单列表确认。"), true);
        return;
    }

    if (m_pending->kind == PendingKind::Start && msgType == START_CHARGING_ACK) {
        const PendingRequest pending = *m_pending;
        StartChargingResult result;
        result.requestId = pending.context.requestId;
        result.operationId = pending.context.operationId;
        result.orderId = payload.value(QStringLiteral("orderNo")).toString();
        result.stationId = pending.stationId;
        result.chargerId = pending.chargerId;
        result.priceCentsPerKwhSnapshot =
            centsField(payload, "priceCents", "price").value_or(0);
        const QString started = payload.value(QStringLiteral("startedAt")).toString();
        result.startedAtUtc = QDateTime::fromString(started, Qt::ISODate).toUTC();
        finishPending();
        emit chargingStarted(pending.context, result);
    }
}

void RealChargingNetworkApi::publishConfirmation(const PendingRequest &pending)
{
    const bool online = boolField(pending.charger, "online");
    const int businessStatus = pending.charger.value(QStringLiteral("businessStatus")).toInt(-1);
    ChargeConfirmationSnapshot snapshot;
    snapshot.stationId = pending.stationId;
    snapshot.chargerId = pending.chargerId;
    snapshot.stationName = stringField(pending.station, {"stationName", "name"});
    snapshot.stationAddress = stringField(pending.station, {"address", "addr"});
    snapshot.chargerCode = stringField(pending.charger, {"chargerCode", "chargerId"});
    snapshot.chargerType = stringField(pending.charger, {"type"});
    snapshot.powerKw = numberField(pending.charger, "powerKw");
    if (!snapshot.powerKw) snapshot.powerKw = numberField(pending.charger, "power");
    snapshot.priceCentsPerKwh = centsField(pending.station, "priceCents", "price");
    snapshot.walletBalanceCents = pending.balanceCents;
    snapshot.hasActiveOrder = !pending.activeOrderId.isEmpty();
    snapshot.activeOrderId = pending.activeOrderId;
    snapshot.canStart = online && businessStatus == CHARGER_IDLE;
    snapshot.startOperationSupported = capabilities().canStartChargingSafely();
    snapshot.canRecharge = true;
    if (snapshot.hasActiveOrder) {
        snapshot.disabledReason = QStringLiteral("当前账号已有进行中的订单。");
    } else if (!snapshot.canStart) {
        snapshot.disabledReason = online ? QStringLiteral("电桩当前不可用。")
                                         : QStringLiteral("电桩离线。");
    }
    emit confirmationReady(pending.context, snapshot);
}

void RealChargingNetworkApi::handleConnectionStateChanged(ConnectionState state)
{
    if (state != ConnectionState::Connected && m_pending) {
        const bool mutation = m_pending->kind == PendingKind::Start;
        failPending(QStringLiteral("connection-lost"),
                    QStringLiteral("服务器连接已断开。"), !mutation, mutation);
    }
}

void RealChargingNetworkApi::handleTimeout()
{
    if (!m_pending) return;
    const bool mutation = m_pending->kind == PendingKind::Start;
    failPending(QStringLiteral("request-timeout"),
                mutation ? QStringLiteral("启动结果未知，请查询订单确认，勿重复启动。")
                         : QStringLiteral("充电确认查询超时。"), !mutation, mutation);
}

void RealChargingNetworkApi::finishPending()
{
    if (!m_pending) return;
    if (m_pending->timer) {
        m_pending->timer->stop();
        m_pending->timer->deleteLater();
    }
    m_pending.reset();
}

void RealChargingNetworkApi::failPending(const QString &code,
                                         const QString &message,
                                         bool retryable, bool resultUnknown)
{
    if (!m_pending) return;
    const RequestContext context = m_pending->context;
    finishPending();
    emitFailure(context, code, message, retryable, resultUnknown);
}

void RealChargingNetworkApi::emitFailure(const RequestContext &context,
                                         const QString &code,
                                         const QString &message,
                                         bool retryable, bool resultUnknown)
{
    ClientError error;
    error.requestId = context.requestId;
    error.operationId = context.operationId;
    error.code = code;
    error.displayMessage = message;
    error.retryable = retryable;
    error.resultUnknown = resultUnknown;
    emit requestFailed(error);
}

QString RealChargingNetworkApi::stringField(const QJsonObject &object,
                                            std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const QJsonValue value = object.value(QLatin1String(key));
        if (value.isString()) return value.toString().trimmed();
    }
    return {};
}

bool RealChargingNetworkApi::boolField(const QJsonObject &object, const char *key)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (value.isBool()) return value.toBool();
    bool ok = false;
    const int number = value.toString().toInt(&ok);
    return ok ? number == 1 : value.toInt(-1) == 1;
}

std::optional<qint64> RealChargingNetworkApi::centsField(const QJsonObject &object,
                                                         const char *centsKey,
                                                         const char *yuanKey)
{
    const QJsonValue cents = object.value(QLatin1String(centsKey));
    if (cents.isDouble()) return qRound64(cents.toDouble());
    if (cents.isString()) {
        bool ok = false;
        const qint64 value = cents.toString().toLongLong(&ok);
        if (ok) return value;
    }
    const QJsonValue yuan = object.value(QLatin1String(yuanKey));
    bool ok = false;
    const double value = yuan.isDouble() ? yuan.toDouble() : yuan.toString().toDouble(&ok);
    if (yuan.isDouble()) ok = true;
    return ok ? std::optional<qint64>(qRound64(value * 100.0)) : std::nullopt;
}

std::optional<double> RealChargingNetworkApi::numberField(const QJsonObject &object,
                                                          const char *key)
{
    const QJsonValue value = object.value(QLatin1String(key));
    bool ok = false;
    const double number = value.isDouble() ? value.toDouble() : value.toString().toDouble(&ok);
    if (value.isDouble()) ok = true;
    return ok && std::isfinite(number) ? std::optional<double>(number) : std::nullopt;
}
