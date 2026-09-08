#include "paymentwindow.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
QHBoxLayout *paymentRow(QWidget *parent, const QString &caption, QLabel **value)
{
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 11, 0, 11);
    auto *name = new QLabel(caption, parent);
    name->setStyleSheet(QStringLiteral("color:#748096;font-size:14px;"));
    *value = new QLabel(QStringLiteral("--"), parent);
    (*value)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    (*value)->setStyleSheet(QStringLiteral("color:#34415A;font-size:14px;font-weight:600;"));
    row->addWidget(name);
    row->addWidget(*value, 1);
    return row;
}
}

PaymentWindow::PaymentWindow(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("paymentPage"));
    setStyleSheet(QStringLiteral(
        "#paymentPage{background:#F8FAFE;color:#0A1835;}"
        "#backButton{background:transparent;border:none;color:#13223F;font-size:28px;}"
        "#paymentCard{background:white;border:1px solid #E4EAF2;border-radius:16px;}"
        "#rechargeButton{background:transparent;border:none;color:#0874F9;font-weight:700;padding:4px;}"
        "#payButton{background:#0874F9;border:none;border-radius:14px;color:white;font-size:17px;font-weight:700;}"
        "#payButton:disabled{background:#B8C7DA;}"
        "#paymentNotice{background:#FFF7E8;border:1px solid #FFE0A6;border-radius:12px;color:#A86600;padding:10px;}"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 10, 16, 16);
    root->setSpacing(10);
    auto *header = new QHBoxLayout;
    auto *back = new QPushButton(QStringLiteral("‹"), this);
    back->setObjectName(QStringLiteral("backButton"));
    back->setFixedSize(38, 38);
    m_title = new QLabel(tr("订单支付"), this);
    m_title->setAlignment(Qt::AlignCenter);
    m_title->setStyleSheet(QStringLiteral("font-size:21px;font-weight:700;"));
    auto *space = new QWidget(this);
    space->setFixedSize(38, 38);
    header->addWidget(back);
    header->addWidget(m_title, 1);
    header->addWidget(space);
    root->addLayout(header);

    auto *caption = new QLabel(tr("待支付金额"), this);
    caption->setAlignment(Qt::AlignCenter);
    caption->setStyleSheet(QStringLiteral("color:#7B879B;font-size:14px;margin-top:16px;"));
    root->addWidget(caption);
    m_amount = new QLabel(QStringLiteral("--"), this);
    m_amount->setAlignment(Qt::AlignCenter);
    m_amount->setStyleSheet(QStringLiteral("color:#0874F9;font-size:42px;font-weight:700;margin:2px 0;"));
    root->addWidget(m_amount);
    m_description = new QLabel(this);
    m_description->setAlignment(Qt::AlignCenter);
    m_description->setWordWrap(true);
    m_description->setStyleSheet(QStringLiteral("color:#52627A;font-size:13px;padding:0 12px 12px 12px;"));
    root->addWidget(m_description);

    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("paymentCard"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 12, 16, 12);
    cardLayout->setSpacing(0);
    auto *cardTitle = new QLabel(tr("支付信息"), card);
    cardTitle->setStyleSheet(QStringLiteral("font-size:16px;font-weight:700;color:#13223F;padding:2px 0 6px 0;"));
    cardLayout->addWidget(cardTitle);
    cardLayout->addLayout(paymentRow(card, tr("订单类型"), &m_orderType));
    QLabel *method = nullptr;
    cardLayout->addLayout(paymentRow(card, tr("支付方式"), &method));
    method->setText(tr("钱包支付"));
    auto *balanceRow = paymentRow(card, tr("钱包余额"), &m_balance);
    m_recharge = new QPushButton(tr("去充值"), card);
    m_recharge->setObjectName(QStringLiteral("rechargeButton"));
    balanceRow->addWidget(m_recharge);
    cardLayout->addLayout(balanceRow);
    cardLayout->addLayout(paymentRow(card, tr("支付后余额"), &m_afterBalance));
    root->addWidget(card);

    m_message = new QLabel(this);
    m_message->setObjectName(QStringLiteral("paymentNotice"));
    m_message->setWordWrap(true);
    m_message->setAlignment(Qt::AlignCenter);
    root->addWidget(m_message);
    root->addStretch();
    m_pay = new QPushButton(tr("确认支付"), this);
    m_pay->setObjectName(QStringLiteral("payButton"));
    m_pay->setMinimumHeight(54);
    root->addWidget(m_pay);

    connect(back, &QPushButton::clicked, this, &PaymentWindow::backRequested);
    connect(m_recharge, &QPushButton::clicked, this, &PaymentWindow::rechargeRequested);
    connect(m_pay, &QPushButton::clicked, this, [this] {
        if (m_state.businessId.isEmpty()) return;
        if (m_state.status == PaymentViewStatus::ResultUnknown
            && m_state.canRecoverResult) {
            emit paymentResultRefreshRequested(m_state.businessId);
            return;
        }
        if (!m_state.canPay && m_state.canRecharge) {
            emit rechargeRequested();
            return;
        }
        if (m_state.canPay) emit payRequested(m_state.businessId, m_state.purpose);
    });
}

void PaymentWindow::render(const PaymentViewState &state)
{
    m_state = state;
    m_title->setText(state.titleText.isEmpty() ? tr("订单支付") : state.titleText);
    m_description->setText(state.descriptionText);
    m_amount->setText(state.amountText.isEmpty() ? QStringLiteral("--") : state.amountText);
    m_orderType->setText(state.purpose == PaymentPurpose::Reservation
                             ? tr("预约押金") : tr("充电订单"));
    m_balance->setText(state.balanceText.isEmpty() ? QStringLiteral("--") : state.balanceText);
    m_afterBalance->setText(state.balanceAfterPaymentText.isEmpty()
                                ? QStringLiteral("--") : state.balanceAfterPaymentText);
    m_message->setText(state.message);
    m_message->setVisible(!state.message.isEmpty());

    const bool busy = state.status == PaymentViewStatus::Submitting
                      || state.status == PaymentViewStatus::ResultUnknown;
    m_pay->setEnabled((!busy && (state.canPay || state.canRecharge))
                      || state.canRecoverResult);
    if (state.status == PaymentViewStatus::ResultUnknown)
        m_pay->setText(tr("查询支付结果"));
    else if (state.status == PaymentViewStatus::Submitting)
        m_pay->setText(tr("正在支付…"));
    else if (!state.canPay && state.canRecharge)
        m_pay->setText(tr("余额不足，去充值"));
    else
        m_pay->setText(tr("确认支付 %1").arg(state.amountText));
    m_recharge->setEnabled(state.canRecharge && !busy);
    m_recharge->setVisible(state.canRecharge);
}
