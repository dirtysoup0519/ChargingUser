#include "walletuibinder.h"

#include "common/clienterror.h"
#include "common/requestcontext.h"
#include "modules/wallet/iwalletservice.h"
#include "modules/wallet/wallettypes.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>
#include <QUuid>

#include <algorithm>

WalletUiBinder::WalletUiBinder(IWalletService *service, QObject *parent)
    : QObject(parent), m_service(service)
{
    Q_ASSERT(m_service);
    connect(m_service, &IWalletService::walletReady,
            this, &WalletUiBinder::handleWalletReady);
    connect(m_service, &IWalletService::moneyOperationSucceeded,
            this, &WalletUiBinder::handleOperationSucceeded);
    connect(m_service, &IWalletService::requestFailed,
            this, &WalletUiBinder::handleFailure);
}

WalletViewState WalletUiBinder::currentState() const
{
    return m_state;
}

void WalletUiBinder::setAccountId(const QString &accountId)
{
    const QString normalized = accountId.trimmed();
    if (normalized == m_accountId) return;
    if (!m_requestId.isEmpty()) m_service->cancel(m_requestId);
    m_accountId = normalized;
    m_requestId.clear();
    m_operationId.clear();
    m_submittedAmountCents = 0;
    m_state = WalletViewState{};
    loadRecharges();
    publish();
}

void WalletUiBinder::activate()
{
    if (m_accountId.isEmpty() || m_state.status == WalletPageStatus::Loading
        || m_state.status == WalletPageStatus::Submitting) {
        return;
    }
    m_requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_operationId.clear();
    m_state.status = WalletPageStatus::Loading;
    m_state.message = QStringLiteral("正在刷新钱包余额和流水…");
    m_state.canSubmit = false;
    publish();
    m_service->queryWallet({m_requestId, {}});
}

void WalletUiBinder::rechargeRequested(const QString &amountText)
{
    if (!m_state.canSubmit || !m_requestId.isEmpty()) return;
    qint64 amountCents = 0;
    if (!parseAmountCents(amountText, &amountCents)) {
        m_state.status = WalletPageStatus::Error;
        m_state.message = QStringLiteral("请输入 0.01 至 100000.00 元的有效金额，最多两位小数。");
        publish();
        return;
    }
    m_requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_operationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_submittedAmountCents = amountCents;
    m_state.status = WalletPageStatus::Submitting;
    m_state.message = QStringLiteral("正在提交充值请求…");
    m_state.canSubmit = false;
    publish();
    m_service->recharge({m_requestId, m_operationId}, amountCents);
}

void WalletUiBinder::handleWalletReady(const RequestContext &context,
                                       const WalletSnapshot &snapshot)
{
    if (context.requestId != m_requestId || snapshot.accountId != m_accountId) return;
    m_requestId.clear();
    m_operationId.clear();
    m_state.status = WalletPageStatus::Ready;
    m_state.balanceText = moneyText(snapshot.balanceCents);
    m_state.recentTransactions =
        mergeTransactions(snapshot.recentTransactions, m_recentRecharges);
    // 服务端流水表不支持查询时（transactionsNotice 非空）不打扰用户：
    // 充值账单由本地 216 回执记录提供，余额仍是服务端权威值。
    m_state.message = m_state.recentTransactions.isEmpty()
                          ? QStringLiteral("余额已同步，暂无钱包流水。")
                          : QStringLiteral("余额与最近钱包流水已同步。\n%1")
                                .arg(transactionText(m_state.recentTransactions));
    m_state.canSubmit = true;
    publish();
}

void WalletUiBinder::handleOperationSucceeded(const RequestContext &context,
                                              const MoneyOperationResult &result)
{
    if (context.requestId != m_requestId || context.operationId != m_operationId) return;
    m_requestId.clear();
    m_operationId.clear();
    // 216 回执即服务端确认的充值账单：服务端流水表可能无法查询，
    // 本次会话的充值记录由此保留并展示。
    WalletTransaction recharge;
    recharge.transactionId = result.transactionId;
    recharge.type = WalletTransactionType::Recharge;
    recharge.amountCents = m_submittedAmountCents;
    recharge.balanceAfterCents = result.balanceCents;
    recharge.createdAtUtc = QDateTime::currentDateTimeUtc();
    const bool duplicate = std::any_of(
        m_recentRecharges.begin(), m_recentRecharges.end(),
        [&](const WalletTransaction &existing) {
            return (!recharge.transactionId.isEmpty()
                    && existing.transactionId == recharge.transactionId)
                   || (existing.amountCents == recharge.amountCents
                       && existing.balanceAfterCents == recharge.balanceAfterCents
                       && existing.createdAtUtc.secsTo(recharge.createdAtUtc) == 0);
        });
    if (!duplicate) {
        m_recentRecharges.append(recharge);
        saveRecharges();
    }
    m_submittedAmountCents = 0;
    m_state.status = WalletPageStatus::Loading;
    m_state.balanceText = moneyText(result.balanceCents);
    m_state.recentTransactions = mergeTransactions({}, m_recentRecharges);
    m_state.message = QStringLiteral("充值成功，正在重新同步余额和钱包流水…");
    m_state.canSubmit = false;
    m_requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    publish();
    emit profileRefreshRequested();
    m_service->queryWallet({m_requestId, {}});
}

void WalletUiBinder::handleFailure(const ClientError &error)
{
    if (error.requestId != m_requestId) return;
    m_requestId.clear();
    m_operationId.clear();
    m_state.status = error.resultUnknown ? WalletPageStatus::ResultUnknown
                                         : WalletPageStatus::Error;
    m_state.message = error.displayMessage.isEmpty()
                          ? QStringLiteral("钱包请求失败。")
                          : error.displayMessage;
    // 已确认的余额与本次会话充值账单不因同步失败被吞掉，保持可见。
    m_state.recentTransactions = mergeTransactions({}, m_recentRecharges);
    // 结果未知时保持锁定；重新进入页面会先查询权威余额再恢复提交能力。
    m_state.canSubmit = !error.resultUnknown && !m_state.balanceText.isEmpty();
    publish();
}

void WalletUiBinder::publish()
{
    emit stateChanged(m_state);
}

QString WalletUiBinder::moneyText(qint64 cents)
{
    return QStringLiteral("¥%1").arg(cents / 100.0, 0, 'f', 2);
}

QVector<WalletTransaction> WalletUiBinder::mergeTransactions(
    const QVector<WalletTransaction> &serverRows,
    const QVector<WalletTransaction> &localRecharges)
{
    QVector<WalletTransaction> merged = serverRows;
    for (const WalletTransaction &local : localRecharges) {
        const bool duplicate = std::any_of(
            merged.begin(), merged.end(), [&](const WalletTransaction &existing) {
                return (!local.transactionId.isEmpty()
                        && existing.transactionId == local.transactionId)
                       || (existing.type == WalletTransactionType::Recharge
                           && existing.amountCents == local.amountCents
                           && existing.createdAtUtc.isValid()
                           && local.createdAtUtc.isValid()
                           && qAbs(existing.createdAtUtc.secsTo(local.createdAtUtc)) <= 2);
            });
        if (!duplicate) merged.append(local);
    }
    std::sort(merged.begin(), merged.end(),
              [](const WalletTransaction &left, const WalletTransaction &right) {
        if (left.createdAtUtc.isValid() != right.createdAtUtc.isValid())
            return left.createdAtUtc.isValid();
        return left.createdAtUtc > right.createdAtUtc;
    });
    return merged;
}

void WalletUiBinder::loadRecharges()
{
    m_recentRecharges.clear();
    if (m_accountId.isEmpty()) return;
    QSettings settings(QStringLiteral("ChargingUser"), QStringLiteral("ChargingUser"));
    const QByteArray stored = settings
        .value(QStringLiteral("wallet/recharges/%1").arg(m_accountId))
        .toByteArray();
    const QJsonDocument document = QJsonDocument::fromJson(stored);
    if (!document.isArray()) return;
    for (const QJsonValue &value : document.array()) {
        if (!value.isObject()) continue;
        const QJsonObject record = value.toObject();
        WalletTransaction recharge;
        recharge.type = WalletTransactionType::Recharge;
        recharge.transactionId =
            record.value(QStringLiteral("transactionId")).toString();
        recharge.amountCents = static_cast<qint64>(
            record.value(QStringLiteral("amountCents")).toDouble());
        recharge.balanceAfterCents = static_cast<qint64>(
            record.value(QStringLiteral("balanceAfterCents")).toDouble());
        const qint64 createdAtMs = static_cast<qint64>(
            record.value(QStringLiteral("createdAtMs")).toDouble());
        recharge.createdAtUtc =
            QDateTime::fromMSecsSinceEpoch(createdAtMs, Qt::UTC);
        if (recharge.amountCents > 0 && recharge.createdAtUtc.isValid())
            m_recentRecharges.append(recharge);
    }
    std::sort(m_recentRecharges.begin(), m_recentRecharges.end(),
              [](const WalletTransaction &left, const WalletTransaction &right) {
        return left.createdAtUtc > right.createdAtUtc;
    });
    constexpr int kMaxStoredRecharges = 50;
    if (m_recentRecharges.size() > kMaxStoredRecharges)
        m_recentRecharges.resize(kMaxStoredRecharges);
}

void WalletUiBinder::saveRecharges()
{
    if (m_accountId.isEmpty()) return;
    QJsonArray records;
    for (const WalletTransaction &recharge : m_recentRecharges) {
        records.append(QJsonObject{
            {QStringLiteral("transactionId"), recharge.transactionId},
            {QStringLiteral("amountCents"),
             static_cast<double>(recharge.amountCents)},
            {QStringLiteral("balanceAfterCents"),
             static_cast<double>(recharge.balanceAfterCents)},
            {QStringLiteral("createdAtMs"),
             static_cast<double>(
                 recharge.createdAtUtc.toMSecsSinceEpoch())}});
    }
    QSettings settings(QStringLiteral("ChargingUser"), QStringLiteral("ChargingUser"));
    settings.setValue(
        QStringLiteral("wallet/recharges/%1").arg(m_accountId),
        QString::fromUtf8(QJsonDocument(records).toJson(QJsonDocument::Compact)));
}

QString WalletUiBinder::transactionText(const QVector<WalletTransaction> &transactions)
{
    QStringList lines;
    for (const WalletTransaction &transaction : transactions) {
        QString type = QStringLiteral("其他");
        if (transaction.type == WalletTransactionType::Recharge) {
            type = QStringLiteral("充值");
        } else if (transaction.type == WalletTransactionType::Payment) {
            type = QStringLiteral("支付");
        } else if (transaction.type == WalletTransactionType::Refund) {
            type = QStringLiteral("退款");
        } else if (transaction.type == WalletTransactionType::Deposit) {
            type = QStringLiteral("预约押金");
        }
        const QString time = transaction.createdAtUtc.isValid()
                                 ? transaction.createdAtUtc.toLocalTime()
                                       .toString(QStringLiteral("MM-dd hh:mm"))
                                 : QStringLiteral("时间未知");
        lines.append(QStringLiteral("%1  %2  %3")
                         .arg(time, type, moneyText(transaction.amountCents)));
    }
    return lines.join(QStringLiteral("\n"));
}

bool WalletUiBinder::parseAmountCents(const QString &text, qint64 *amountCents)
{
    static const QRegularExpression pattern(
        QStringLiteral("^(?:0|[1-9]\\d{0,5})(?:\\.\\d{1,2})?$"));
    const QString normalized = text.trimmed();
    if (!pattern.match(normalized).hasMatch()) return false;
    bool ok = false;
    const double yuan = normalized.toDouble(&ok);
    if (!ok || yuan < 0.01 || yuan > 100000.0) return false;
    *amountCents = qRound64(yuan * 100.0);
    return *amountCents > 0 && *amountCents <= 10000000;
}
