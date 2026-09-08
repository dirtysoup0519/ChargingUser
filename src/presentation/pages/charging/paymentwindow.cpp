#include "paymentwindow.h"
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

PaymentWindow::PaymentWindow(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("paymentPage"));
    setStyleSheet("#paymentPage{background:#F6F8FC;color:#0A1835;}"
                  "QFrame{background:white;border:1px solid #E1E8F2;border-radius:16px;}"
                  "#backButton{background:transparent;border:none;color:#13223F;font-size:28px;}"
                  "#rechargeButton{background:transparent;border:1px solid #0874F9;border-radius:10px;color:#0874F9;font-weight:600;}"
                  "#payButton{background:#0874F9;border:none;border-radius:13px;color:white;font-size:17px;font-weight:700;}"
                  "#payButton:disabled{background:#B8C7DA;}");
    auto *root = new QVBoxLayout(this); root->setContentsMargins(14,12,14,16); root->setSpacing(14);
    auto *header = new QHBoxLayout;
    auto *back = new QPushButton(QStringLiteral("‹"), this); back->setObjectName("backButton"); back->setFixedSize(38,38);
    m_title = new QLabel(tr("订单支付"), this); m_title->setAlignment(Qt::AlignCenter); m_title->setStyleSheet("font-size:20px;font-weight:700;");
    auto *space = new QWidget(this); space->setFixedSize(38,38); header->addWidget(back); header->addWidget(m_title,1); header->addWidget(space); root->addLayout(header);
    auto *amountCard = new QFrame(this); auto *amountLayout = new QVBoxLayout(amountCard);
    auto *caption = new QLabel(tr("待支付金额"), amountCard); caption->setAlignment(Qt::AlignCenter); caption->setStyleSheet("color:#7B879B;");
    m_amount = new QLabel(QStringLiteral("--"), amountCard); m_amount->setAlignment(Qt::AlignCenter); m_amount->setStyleSheet("color:#0874F9;font-size:38px;font-weight:700;");
    m_description = new QLabel(amountCard); m_description->setAlignment(Qt::AlignCenter); m_description->setWordWrap(true); m_description->setStyleSheet("color:#52627A;");
    amountLayout->addWidget(caption); amountLayout->addWidget(m_amount); amountLayout->addWidget(m_description); root->addWidget(amountCard);
    auto *wallet = new QFrame(this); auto *walletLayout = new QHBoxLayout(wallet);
    auto *balanceCaption = new QLabel(tr("钱包余额"), wallet); m_balance = new QLabel(QStringLiteral("--"), wallet); m_balance->setAlignment(Qt::AlignRight|Qt::AlignVCenter); m_balance->setStyleSheet("font-size:18px;font-weight:700;");
    m_recharge = new QPushButton(tr("去充值"), wallet); m_recharge->setObjectName("rechargeButton"); m_recharge->setMinimumSize(72,36);
    walletLayout->addWidget(balanceCaption); walletLayout->addWidget(m_balance,1); walletLayout->addWidget(m_recharge); root->addWidget(wallet);
    m_message = new QLabel(this); m_message->setWordWrap(true); m_message->setAlignment(Qt::AlignCenter); m_message->setStyleSheet("color:#A86600;"); root->addWidget(m_message); root->addStretch();
    m_pay = new QPushButton(tr("确认支付"), this); m_pay->setObjectName("payButton"); m_pay->setMinimumHeight(52); root->addWidget(m_pay);
    connect(back, &QPushButton::clicked, this, &PaymentWindow::backRequested);
    connect(m_recharge, &QPushButton::clicked, this, &PaymentWindow::rechargeRequested);
    connect(m_pay, &QPushButton::clicked, this, [this]{
        if (m_state.businessId.isEmpty()) return;
        if (m_state.status == PaymentViewStatus::ResultUnknown
            && m_state.canRecoverResult) {
            emit paymentResultRefreshRequested(m_state.businessId);
            return;
        }
        if (m_state.canPay) emit payRequested(m_state.businessId, m_state.purpose);
    });
}

void PaymentWindow::render(const PaymentViewState &state)
{
    m_state = state; m_title->setText(state.titleText.isEmpty() ? tr("订单支付") : state.titleText);
    m_description->setText(state.descriptionText); m_amount->setText(state.amountText); m_balance->setText(state.balanceText);
    m_message->setText(state.message); m_message->setVisible(!state.message.isEmpty());
    const bool submitting = state.status == PaymentViewStatus::Submitting || state.status == PaymentViewStatus::ResultUnknown;
    m_pay->setEnabled((state.canPay && !submitting) || state.canRecoverResult);
    m_pay->setText(state.status == PaymentViewStatus::ResultUnknown
                       ? tr("查询支付结果")
                       : submitting ? tr("正在确认支付…") : tr("确认支付"));
    m_recharge->setEnabled(state.canRecharge && !submitting);
}
