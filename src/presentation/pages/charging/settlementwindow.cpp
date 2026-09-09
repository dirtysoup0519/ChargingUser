#include "settlementwindow.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
QLabel *informationRow(QVBoxLayout *layout, const QString &caption)
{
    auto *row = new QWidget;
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 11, 0, 11);
    auto *name = new QLabel(caption, row);
    name->setStyleSheet(QStringLiteral("color:#748096;font-size:14px;"));
    auto *value = new QLabel(QStringLiteral("--"), row);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    value->setWordWrap(true);
    value->setStyleSheet(QStringLiteral("color:#34415A;font-size:14px;font-weight:600;"));
    rowLayout->addWidget(name);
    rowLayout->addWidget(value, 1);
    layout->addWidget(row);
    return value;
}
}

SettlementWindow::SettlementWindow(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("settlementPage"));
    setStyleSheet(QStringLiteral(
        "#settlementPage{background:#F8FAFE;color:#0A1835;}"
        "#backButton{border:none;background:transparent;font-size:28px;color:#13223F;}"
        "#informationCard{background:white;border:1px solid #E4EAF2;border-radius:16px;}"
        "#payButton{border:none;border-radius:14px;background:#0874F9;color:white;font-size:17px;font-weight:700;}"
        "#payButton:disabled{background:#B8C7DA;}"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 10, 16, 16);
    root->setSpacing(8);

    auto *header = new QHBoxLayout;
    auto *back = new QPushButton(QStringLiteral("‹"), this);
    back->setObjectName(QStringLiteral("backButton"));
    back->setFixedSize(38, 38);
    auto *title = new QLabel(tr("订单结算"), this);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral("font-size:21px;font-weight:700;"));
    auto *placeholder = new QWidget(this);
    placeholder->setFixedSize(38, 38);
    header->addWidget(back);
    header->addWidget(title, 1);
    header->addWidget(placeholder);
    root->addLayout(header);

    auto *hero = new QLabel(this);
    hero->setMinimumHeight(145);
    hero->setMaximumHeight(165);
    hero->setAlignment(Qt::AlignCenter);
    hero->setPixmap(QPixmap(QStringLiteral(":/images/charging_complete_hero.png"))
                        .scaled(330, 155, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    root->addWidget(hero);
    auto *completeTitle = new QLabel(tr("充电完成"), this);
    completeTitle->setAlignment(Qt::AlignCenter);
    completeTitle->setStyleSheet(QStringLiteral("font-size:25px;font-weight:700;color:#101D38;"));
    root->addWidget(completeTitle);
    auto *subtitle = new QLabel(tr("本次充电已结束，请完成订单支付"), this);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setStyleSheet(QStringLiteral("font-size:13px;color:#778398;"));
    root->addWidget(subtitle);

    m_amount = new QLabel(QStringLiteral("--"), this);
    m_amount->setAlignment(Qt::AlignCenter);
    m_amount->setStyleSheet(QStringLiteral("font-size:38px;font-weight:700;color:#0874F9;margin-top:4px;"));
    root->addWidget(m_amount);
    auto *amountCaption = new QLabel(tr("充电费用"), this);
    amountCaption->setAlignment(Qt::AlignCenter);
    amountCaption->setStyleSheet(QStringLiteral("font-size:14px;color:#778398;"));
    root->addWidget(amountCaption);

    auto *informationCard = new QFrame(this);
    informationCard->setObjectName(QStringLiteral("informationCard"));
    auto *informationLayout = new QVBoxLayout(informationCard);
    informationLayout->setContentsMargins(16, 10, 16, 10);
    informationLayout->setSpacing(0);
    auto *informationTitle = new QLabel(tr("订单信息"), informationCard);
    informationTitle->setStyleSheet(QStringLiteral("font-size:16px;font-weight:700;color:#13223F;padding:4px 0;"));
    informationLayout->addWidget(informationTitle);
    m_chargingTime = informationRow(informationLayout, tr("充电时间"));
    m_paymentMethod = informationRow(informationLayout, tr("支付方式"));
    m_chargerInfo = informationRow(informationLayout, tr("充电桩"));
    root->addWidget(informationCard);

    m_message = new QLabel(this);
    m_message->setAlignment(Qt::AlignCenter);
    m_message->setWordWrap(true);
    m_message->setStyleSheet(QStringLiteral("color:#A86600;font-size:13px;"));
    root->addWidget(m_message);
    root->addStretch();
    m_pay = new QPushButton(tr("前往支付"), this);
    m_pay->setObjectName(QStringLiteral("payButton"));
    m_pay->setMinimumHeight(52);
    root->addWidget(m_pay);

    connect(back, &QPushButton::clicked, this, &SettlementWindow::backRequested);
    connect(m_pay, &QPushButton::clicked, this, [this] {
        if (m_state.canPay && !m_state.orderId.isEmpty())
            emit paymentRequested(m_state.orderId);
    });
}

void SettlementWindow::render(const SettlementViewState &state)
{
    m_state = state;
    const QString payableText = state.payableText.isEmpty()
                                    ? state.amountText : state.payableText;
    m_amount->setText(payableText.isEmpty() ? QStringLiteral("--") : payableText);
    m_chargingTime->setText(state.chargingTimeText.isEmpty()
                                ? state.durationText : state.chargingTimeText);
    m_paymentMethod->setText(state.paymentMethodText.isEmpty()
                                 ? tr("钱包支付") : state.paymentMethodText);
    m_chargerInfo->setText(state.chargerInfoText.isEmpty()
                               ? tr("%1 · %2号桩").arg(state.stationName, state.chargerCode)
                               : state.chargerInfoText);
    m_message->setText(state.message);
    m_message->setVisible(!state.message.isEmpty());
    m_pay->setEnabled(state.canPay);
}
