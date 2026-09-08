#include "realwalletnetworkapi.h"

#include "backendclient.h"
#include "protocol.h"

#include <QDateTime>
#include <QJsonArray>
#include <QTimer>

#include <cmath>

namespace {

qint64 parseCents(const QJsonObject &object, const QString &centsKey,
                  const QString &yuanKey, bool *ok)
{
    *ok = false;
    const QJsonValue cents = object.value(centsKey);
    if (cents.isDouble()) {
        *ok = true;
        return qRound64(cents.toDouble());
    }
    if (cents.isString()) {
        const qint64 value = cents.toString().toLongLong(ok);
        if (*ok) return value;
    }
    const QJsonValue yuan = object.value(yuanKey);
    double value = 0.0;
    if (yuan.isDouble()) {
        value = yuan.toDouble();
        *ok = true;
    } else if (yuan.isString()) {
        value = yuan.toString().toDouble(ok);
    }
    return *ok ? qRound64(value * 100.0) : 0;
}

QDateTime parseTime(const QJsonValue &value)
{
    if (!value.isString()) return {};
    QDateTime parsed = QDateTime::fromString(value.toString(), Qt::ISODate);
    if (!parsed.isValid()) {
        parsed = QDateTime::fromString(value.toString(),
                                       QStringLiteral("yyyy-MM-dd hh:mm:ss"));
        parsed.setTimeSpec(Qt::UTC);
    }
    return parsed.toUTC();
}

} // namespace

RealWalletNetworkApi::RealWalletNetworkApi(BackendClient *backend, QObject *parent)
    : IWalletNetworkApi(parent), m_backend(backend)
{
    Q_ASSERT(m_backend);
    connect(m_backend, &BackendClient::frameReceived,
            this, &RealWalletNetworkApi::handleFrame);
    connect(m_backend, &BackendClient::connectionStateChanged,
            this, &RealWalletNetworkApi::handleConnectionStateChanged);
}

WalletBackendCapabilities RealWalletNetworkApi::capabilities() const
{
    WalletBackendCapabilities value;
    value.walletSnapshotQuery = true;
    value.rechargeMessage = true;
    return value;
}

void RealWalletNetworkApi::setIdentity(const QString &username)
{
    const QString normalized = username.trimmed();
    if (normalized == m_username) return;
    if (m_pending) {
        failPending(QStringLiteral("identity-changed"),
                    QStringLiteral("登录账号已变化。"), false);
    }
    m_username = normalized;
}

void RealWalletNetworkApi::setRequestTimeoutMs(int timeoutMs)
{
    m_requestTimeoutMs = qMax(1, timeoutMs);
}

void RealWalletNetworkApi::queryWallet(const RequestContext &context)
{
    if (!begin(PendingKind::WalletUser, context)) return;
    if (!sendTableQuery(QStringLiteral("user"))) {
        failPending(QStringLiteral("send-failed"),
                    QStringLiteral("钱包查询发送失败。"), true);
    }
}

void RealWalletNetworkApi::recharge(const RequestContext &context,
                                    qint64 amountCents)
{
    if (amountCents <= 0 || !begin(PendingKind::Recharge, context)) return;
    QJsonObject payload{{QStringLiteral("username"), m_username},
                        {QStringLiteral("amountCents"),
                         static_cast<double>(amountCents)},
                        {QStringLiteral("requestId"), context.requestId},
                        {QStringLiteral("operationId"), context.operationId}};
    if (!m_backend->sendFrame(RECHARGE_REQ, payload)) {
        failPending(QStringLiteral("send-failed"),
                    QStringLiteral("充值请求发送失败。"), false, true);
    }
}

void RealWalletNetworkApi::payOrder(const RequestContext &context,
                                    const QString &orderId)
{
    if (orderId.trimmed().isEmpty() || !begin(PendingKind::PayOrder, context)) return;
    QJsonObject payload{{QStringLiteral("orderNo"), orderId.trimmed()},
                        {QStringLiteral("username"), m_username},
                        {QStringLiteral("requestId"), context.requestId},
                        {QStringLiteral("operationId"), context.operationId}};
    if (!m_backend->sendFrame(PAY_REQ, payload)) {
        failPending(QStringLiteral("send-failed"),
                    QStringLiteral("订单支付请求发送失败。"), false, true);
    }
}

void RealWalletNetworkApi::queryOperationResult(const RequestContext &context,
                                                const QString &)
{
    emitFailure(context, QStringLiteral("wallet-operation-query-unsupported"),
                QStringLiteral("服务端未提供资金操作结果查询。"));
}

void RealWalletNetworkApi::cancel(const QString &requestId)
{
    if (m_pending && m_pending->context.requestId == requestId) finishPending();
}

bool RealWalletNetworkApi::begin(PendingKind kind, const RequestContext &context)
{
    if (!context.isValid()) {
        emitFailure(context, QStringLiteral("wallet-invalid-request"),
                    QStringLiteral("钱包请求缺少 requestId。"));
        return false;
    }
    if (m_username.isEmpty()) {
        emitFailure(context, QStringLiteral("wallet-no-identity"),
                    QStringLiteral("请先登录。"));
        return false;
    }
    if (m_backend->connectionState() != ConnectionState::Connected) {
        emitFailure(context, QStringLiteral("not-connected"),
                    QStringLiteral("服务器尚未连接。"), true);
        return false;
    }
    if (m_pending) {
        emitFailure(context, QStringLiteral("wallet-request-in-flight"),
                    QStringLiteral("上一项钱包请求仍在处理中。"), true);
        return false;
    }
    PendingRequest pending;
    pending.kind = kind;
    pending.context = context;
    pending.timer = new QTimer(this);
    pending.timer->setSingleShot(true);
    connect(pending.timer, &QTimer::timeout,
            this, &RealWalletNetworkApi::handleTimeout);
    m_pending = pending;
    m_pending->timer->start(m_requestTimeoutMs);
    return true;
}

bool RealWalletNetworkApi::sendTableQuery(const QString &table)
{
    QJsonObject condition{{QStringLiteral("username"), m_username}};
    QJsonObject payload{{QStringLiteral("table"), table},
                        {QStringLiteral("cond"), condition},
                        {QStringLiteral("requestId"),
                         m_pending->context.requestId}};
    return m_backend->sendFrame(GETDATA, payload);
}

void RealWalletNetworkApi::handleFrame(int msgType, const QJsonObject &payload)
{
    if (!m_pending) return;
    const QString echoed = payload.value(QStringLiteral("requestId")).toString();
    if (!echoed.isEmpty() && echoed != m_pending->context.requestId) return;

    if (msgType == DATA
        && (m_pending->kind == PendingKind::WalletUser
            || m_pending->kind == PendingKind::WalletTransactions)) {
        const QJsonValue data = payload.value(QStringLiteral("data"));
        if (!data.isArray()) {
            failPending(QStringLiteral("bad-response"),
                        QStringLiteral("钱包响应格式错误。"), true);
            return;
        }
        if (m_pending->kind == PendingKind::WalletUser) {
            bool found = false;
            for (const QJsonValue &value : data.toArray()) {
                if (!value.isObject()) continue;
                const QJsonObject row = value.toObject();
                const QString username =
                    row.value(QStringLiteral("username")).toString();
                if (!username.isEmpty() && username != m_username) continue;
                bool ok = false;
                m_pending->balanceCents = parseCents(
                    row, QStringLiteral("balanceCents"),
                    QStringLiteral("balance"), &ok);
                if (ok) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                failPending(QStringLiteral("wallet-user-not-found"),
                            QStringLiteral("未找到当前用户的钱包。"), false);
                return;
            }
            m_pending->kind = PendingKind::WalletTransactions;
            if (!sendTableQuery(QStringLiteral("walletTransaction"))) {
                failPending(QStringLiteral("send-failed"),
                            QStringLiteral("钱包流水查询发送失败。"), true);
            }
            return;
        }

        const PendingRequest pending = *m_pending;
        WalletSnapshot snapshot;
        snapshot.accountId = m_username;
        snapshot.balanceCents = pending.balanceCents;
        snapshot.fetchedAtUtc = QDateTime::currentDateTimeUtc();
        for (const QJsonValue &value : data.toArray()) {
            if (value.isObject()) {
                snapshot.recentTransactions.append(
                    parseTransaction(value.toObject()));
            }
        }
        finishPending();
        emit walletReady(pending.context, snapshot);
        return;
    }

    if (msgType == RECHARGE_ACK && m_pending->kind == PendingKind::Recharge) {
        const PendingRequest pending = *m_pending;
        bool ok = false;
        const qint64 balance = parseCents(
            payload, QStringLiteral("balanceCents"),
            QStringLiteral("balance"), &ok);
        if (!ok) {
            failPending(QStringLiteral("bad-response"),
                        QStringLiteral("充值响应缺少余额。"), false, true);
            return;
        }
        MoneyOperationResult result;
        result.requestId = pending.context.requestId;
        result.operationId = pending.context.operationId;
        result.type = MoneyOperationType::Recharge;
        result.balanceCents = balance;
        result.transactionId =
            payload.value(QStringLiteral("transactionId")).toString();
        finishPending();
        emit moneyOperationSucceeded(pending.context, result);
        return;
    }

    if (msgType == PAY_ACK && m_pending->kind == PendingKind::PayOrder) {
        const PendingRequest pending = *m_pending;
        bool ok = false;
        const qint64 balance = parseCents(
            payload, QStringLiteral("balanceCents"),
            QStringLiteral("balance"), &ok);
        if (!ok) {
            failPending(QStringLiteral("bad-response"),
                        QStringLiteral("支付响应缺少余额。"), false, true);
            return;
        }
        MoneyOperationResult result;
        result.requestId = pending.context.requestId;
        result.operationId = pending.context.operationId;
        result.type = MoneyOperationType::PayOrder;
        result.balanceCents = balance;
        result.orderId = payload.value(QStringLiteral("orderNo")).toString();
        result.transactionId = payload.value(QStringLiteral("transactionId")).toString();
        finishPending();
        emit moneyOperationSucceeded(pending.context, result);
        return;
    }

    if (msgType >= DATA_NOEXIST && msgType <= OP_FORBIDDEN) {
        QString message = payload.value(QStringLiteral("err")).toString();
        if (message.isEmpty()) {
            message = payload.value(QStringLiteral("reason")).toString();
        }
        const QString code = payload.value(QStringLiteral("code")).toString(
            QStringLiteral("server-error"));
        failPending(code,
                    message.isEmpty() ? QStringLiteral("服务器拒绝了钱包请求。")
                                      : message,
                    msgType == DB_ERROR);
    }
}

void RealWalletNetworkApi::handleConnectionStateChanged(ConnectionState state)
{
    if (state != ConnectionState::Connected && m_pending) {
        const bool mutation = m_pending->kind == PendingKind::Recharge
                              || m_pending->kind == PendingKind::PayOrder;
        failPending(QStringLiteral("connection-lost"),
                    QStringLiteral("服务器连接已断开。"), !mutation, mutation);
    }
}

void RealWalletNetworkApi::handleTimeout()
{
    if (!m_pending) return;
    const bool mutation = m_pending->kind == PendingKind::Recharge
                          || m_pending->kind == PendingKind::PayOrder;
    failPending(QStringLiteral("request-timeout"),
                mutation ? QStringLiteral("充值结果未知，请刷新余额后确认，勿重复提交。")
                         : QStringLiteral("钱包查询超时。"),
                !mutation, mutation);
}

void RealWalletNetworkApi::finishPending()
{
    if (!m_pending) return;
    if (m_pending->timer) {
        m_pending->timer->stop();
        m_pending->timer->deleteLater();
    }
    m_pending.reset();
}

void RealWalletNetworkApi::failPending(const QString &code,
                                       const QString &message,
                                       bool retryable, bool resultUnknown)
{
    if (!m_pending) return;
    const RequestContext context = m_pending->context;
    finishPending();
    emitFailure(context, code, message, retryable, resultUnknown);
}

void RealWalletNetworkApi::emitFailure(const RequestContext &context,
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

WalletTransaction RealWalletNetworkApi::parseTransaction(const QJsonObject &record)
{
    WalletTransaction value;
    value.transactionId = record.value(QStringLiteral("id")).toVariant().toString();
    const QString type = record.value(QStringLiteral("type")).toString().toUpper();
    if (type == QLatin1String("RECHARGE")) {
        value.type = WalletTransactionType::Recharge;
    } else if (type == QLatin1String("PAY")
               || type == QLatin1String("PAYMENT")) {
        value.type = WalletTransactionType::Payment;
    } else if (type == QLatin1String("REFUND")) {
        value.type = WalletTransactionType::Refund;
    }
    bool ok = false;
    value.amountCents = parseCents(record, QStringLiteral("amountCents"),
                                   QStringLiteral("amount"), &ok);
    value.balanceAfterCents = parseCents(
        record, QStringLiteral("balanceAfterCents"),
        QStringLiteral("balanceAfter"), &ok);
    value.orderId = record.value(QStringLiteral("orderNo")).toString();
    value.createdAtUtc = parseTime(record.value(QStringLiteral("createdAt")));
    return value;
}
