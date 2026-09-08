#include "walletuibinder.h"

#include "common/clienterror.h"
#include "common/requestcontext.h"
#include "modules/wallet/iwalletservice.h"
#include "modules/wallet/wallettypes.h"

#include <QRegularExpression>
#include <QUuid>

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
    m_state = WalletViewState{};
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
    m_state.message = snapshot.recentTransactions.isEmpty()
                          ? QStringLiteral("余额已同步，暂无钱包流水。")
                          : QStringLiteral("余额与最近钱包流水已同步。");
    m_state.canSubmit = true;
    publish();
}

void WalletUiBinder::handleOperationSucceeded(const RequestContext &context,
                                              const MoneyOperationResult &result)
{
    if (context.requestId != m_requestId || context.operationId != m_operationId) return;
    m_requestId.clear();
    m_operationId.clear();
    m_state.status = WalletPageStatus::Succeeded;
    m_state.balanceText = moneyText(result.balanceCents);
    m_state.message = QStringLiteral("充值成功，余额已更新。");
    m_state.canSubmit = true;
    publish();
    emit profileRefreshRequested();
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
