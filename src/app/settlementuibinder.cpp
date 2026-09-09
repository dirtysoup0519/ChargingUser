#include "settlementuibinder.h"

#include "common/clienterror.h"
#include "common/requestcontext.h"
#include "modules/wallet/iwalletservice.h"
#include "modules/wallet/wallettypes.h"

#include <QDateTime>
#include <QUuid>

SettlementUiBinder::SettlementUiBinder(IWalletService *walletService,
                                       QObject *parent)
    : QObject(parent), m_walletService(walletService)
{
    Q_ASSERT(m_walletService);
    qRegisterMetaType<SettlementViewState>();
    connect(m_walletService, &IWalletService::moneyOperationSucceeded,
            this, &SettlementUiBinder::handleOperationSucceeded);
    connect(m_walletService, &IWalletService::requestFailed,
            this, &SettlementUiBinder::handleFailure);
}

SettlementViewState SettlementUiBinder::currentState() const
{
    return m_state;
}

void SettlementUiBinder::showOrder(const ChargingOrder &order)
{
    if (!m_requestId.isEmpty()) m_walletService->cancel(m_requestId);
    m_requestId.clear();
    m_operationId.clear();
    m_state = SettlementViewState{};
    m_state.orderId = order.orderId;
    m_state.stationName = order.stationName;
    m_state.chargerCode = order.chargerCode;
    m_state.energyText = QStringLiteral("%1 kWh").arg(order.energyKwh, 0, 'f', 2);
    m_state.paymentMethodText = QStringLiteral("钱包支付");
    if (order.startedAtUtc.isValid()) {
        const QDateTime end = order.endedAtUtc.value_or(QDateTime::currentDateTimeUtc());
        const qint64 seconds = qMax<qint64>(0, order.startedAtUtc.secsTo(end));
        m_state.chargingTimeText = QStringLiteral("%1 · %2 分钟")
            .arg(order.startedAtUtc.toLocalTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")))
            .arg(seconds / 60);
    }
    m_state.chargerInfoText = QStringLiteral("%1 · %2 · %3")
        .arg(order.stationName.isEmpty() ? QStringLiteral("未知站点") : order.stationName,
             order.chargerCode.isEmpty() ? QStringLiteral("未知电桩") : order.chargerCode,
             m_state.energyText);
    m_state.amountText = moneyText(order.amountCents);
    if (order.paymentDeadlineUtc) {
        m_state.deadlineText = order.paymentDeadlineUtc->toLocalTime()
                                   .toString(Qt::ISODate);
    }
    if (order.status == OrderStatus::PendingSettlement) {
        m_state.status = SettlementPageStatus::Ready;
        m_state.canPay = true;
        m_state.message = QStringLiteral("订单待支付。");
    } else if (order.status == OrderStatus::Settled) {
        m_state.status = SettlementPageStatus::Settled;
        m_state.message = QStringLiteral("订单已支付。");
    } else {
        m_state.status = SettlementPageStatus::Error;
        m_state.message = QStringLiteral("当前订单不处于待支付状态。");
    }
    publish();
}

void SettlementUiBinder::payRequested()
{
    if (m_state.status != SettlementPageStatus::Ready || !m_state.canPay
        || m_state.orderId.isEmpty() || !m_requestId.isEmpty()) return;
    m_requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_operationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_state.status = SettlementPageStatus::Submitting;
    m_state.canPay = false;
    m_state.message = QStringLiteral("正在支付订单…");
    publish();
    m_walletService->payOrder({m_requestId, m_operationId}, m_state.orderId);
}

void SettlementUiBinder::refreshRequested()
{
    emit orderRefreshRequested();
    emit walletRefreshRequested();
}

void SettlementUiBinder::handleOperationSucceeded(
    const RequestContext &context, const MoneyOperationResult &result)
{
    if (context.requestId != m_requestId || context.operationId != m_operationId
        || result.type != MoneyOperationType::PayOrder
        || (!result.orderId.isEmpty() && result.orderId != m_state.orderId)) return;
    m_requestId.clear();
    m_operationId.clear();
    m_state.status = SettlementPageStatus::Settled;
    m_state.balanceText = moneyText(result.balanceCents);
    m_state.message = QStringLiteral("支付成功。");
    m_state.canPay = false;
    publish();
    emit orderRefreshRequested();
    emit walletRefreshRequested();
}

void SettlementUiBinder::handleFailure(const ClientError &error)
{
    if (error.requestId != m_requestId) return;
    m_requestId.clear();
    m_operationId.clear();
    m_state.status = error.resultUnknown ? SettlementPageStatus::ResultUnknown
                                         : SettlementPageStatus::Error;
    m_state.message = error.displayMessage.isEmpty()
                          ? QStringLiteral("订单支付失败。")
                          : error.displayMessage;
    m_state.canPay = false;
    publish();
}

void SettlementUiBinder::publish()
{
    emit stateChanged(m_state);
}

QString SettlementUiBinder::moneyText(qint64 cents)
{
    return QStringLiteral("¥%1").arg(cents / 100.0, 0, 'f', 2);
}
