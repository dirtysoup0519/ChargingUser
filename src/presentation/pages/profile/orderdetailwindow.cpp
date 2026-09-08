#include "orderdetailwindow.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStyle>
#include <QVBoxLayout>

namespace {
QLabel *detailRow(QVBoxLayout *layout, const QString &caption)
{
    auto *row = new QWidget;
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 10, 0, 10);
    auto *name = new QLabel(caption, row);
    name->setStyleSheet(QStringLiteral("color:#748096;font-size:14px;"));
    auto *value = new QLabel(QStringLiteral("--"), row);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    value->setWordWrap(true);
    value->setStyleSheet(QStringLiteral(
        "color:#34415A;font-size:14px;font-weight:600;"));
    rowLayout->addWidget(name);
    rowLayout->addWidget(value, 1);
    layout->addWidget(row);
    return value;
}

QFrame *makeCard(QWidget *parent, QVBoxLayout **layout)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("detailCard"));
    *layout = new QVBoxLayout(card);
    (*layout)->setContentsMargins(16, 10, 16, 10);
    (*layout)->setSpacing(0);
    return card;
}
}

OrderDetailWindow::OrderDetailWindow(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("orderDetailPage"));
    setStyleSheet(QStringLiteral(
        "#orderDetailPage{background:#F8FAFE;color:#0A1835;}"
        "#backButton{border:none;background:transparent;font-size:28px;color:#13223F;}"
        "#detailCard{background:white;border:1px solid #E4EAF2;border-radius:16px;}"
        "#detailStatus{border-radius:11px;padding:5px 11px;background:#EAF3FF;color:#0874F9;font-weight:700;}"
        "#detailStatus[tone='warning']{background:#FFF3DC;color:#B46A00;}"
        "#detailStatus[tone='success']{background:#E8F8F1;color:#138A5B;}"
        "#detailStatus[tone='neutral']{background:#EEF1F5;color:#667085;}"
        "#detailAction{border:none;border-radius:14px;background:#0874F9;color:white;font-size:17px;font-weight:700;}"
        "#detailAction:disabled{background:#B8C7DA;}"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 14);
    root->setSpacing(8);
    auto *header = new QHBoxLayout;
    auto *back = new QPushButton(QStringLiteral("‹"), this);
    back->setObjectName(QStringLiteral("backButton"));
    back->setFixedSize(38, 38);
    auto *title = new QLabel(tr("订单详情"), this);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral("font-size:21px;font-weight:700;"));
    auto *placeholder = new QWidget(this);
    placeholder->setFixedSize(38, 38);
    header->addWidget(back);
    header->addWidget(title, 1);
    header->addWidget(placeholder);
    root->addLayout(header);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget(scroll);
    auto *body = new QVBoxLayout(content);
    body->setContentsMargins(4, 0, 4, 4);
    body->setSpacing(10);

    auto *hero = new QLabel(content);
    hero->setMinimumHeight(132);
    hero->setMaximumHeight(150);
    hero->setAlignment(Qt::AlignCenter);
    hero->setPixmap(QPixmap(QStringLiteral(":/images/charging_complete_hero.png"))
                        .scaled(310, 145, Qt::KeepAspectRatio,
                                Qt::SmoothTransformation));
    body->addWidget(hero);
    m_kind = new QLabel(tr("订单"), content);
    m_kind->setAlignment(Qt::AlignCenter);
    m_kind->setStyleSheet(QStringLiteral(
        "font-size:24px;font-weight:700;color:#101D38;"));
    body->addWidget(m_kind);
    m_status = new QLabel(QStringLiteral("--"), content);
    m_status->setObjectName(QStringLiteral("detailStatus"));
    m_status->setAlignment(Qt::AlignCenter);
    m_status->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    auto *statusRow = new QHBoxLayout;
    statusRow->addStretch();
    statusRow->addWidget(m_status);
    statusRow->addStretch();
    body->addLayout(statusRow);
    m_amount = new QLabel(QStringLiteral("--"), content);
    m_amount->setAlignment(Qt::AlignCenter);
    m_amount->setStyleSheet(QStringLiteral(
        "font-size:36px;font-weight:700;color:#0874F9;margin-top:3px;"));
    body->addWidget(m_amount);
    auto *amountCaption = new QLabel(tr("订单金额"), content);
    amountCaption->setAlignment(Qt::AlignCenter);
    amountCaption->setStyleSheet(QStringLiteral("color:#778398;font-size:14px;"));
    body->addWidget(amountCaption);

    QVBoxLayout *businessLayout = nullptr;
    auto *businessCard = makeCard(content, &businessLayout);
    auto *businessTitle = new QLabel(tr("订单信息"), businessCard);
    businessTitle->setStyleSheet(QStringLiteral(
        "font-size:16px;font-weight:700;color:#13223F;padding:4px 0;"));
    businessLayout->addWidget(businessTitle);
    m_station = detailRow(businessLayout, tr("充电站"));
    m_charger = detailRow(businessLayout, tr("充电桩"));
    m_createdAt = detailRow(businessLayout, tr("创建时间"));
    m_duration = detailRow(businessLayout, tr("使用时长"));
    m_energy = detailRow(businessLayout, tr("充电电量"));
    body->addWidget(businessCard);

    QVBoxLayout *paymentLayout = nullptr;
    auto *paymentCard = makeCard(content, &paymentLayout);
    auto *paymentTitle = new QLabel(tr("支付信息"), paymentCard);
    paymentTitle->setStyleSheet(QStringLiteral(
        "font-size:16px;font-weight:700;color:#13223F;padding:4px 0;"));
    paymentLayout->addWidget(paymentTitle);
    m_paymentMethod = detailRow(paymentLayout, tr("支付方式"));
    m_orderId = detailRow(paymentLayout, tr("订单编号"));
    body->addWidget(paymentCard);
    m_message = new QLabel(content);
    m_message->setAlignment(Qt::AlignCenter);
    m_message->setWordWrap(true);
    m_message->setStyleSheet(QStringLiteral("color:#A86600;font-size:13px;padding:4px;"));
    body->addWidget(m_message);
    body->addStretch();
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    m_action = new QPushButton(this);
    m_action->setObjectName(QStringLiteral("detailAction"));
    m_action->setMinimumHeight(52);
    root->addWidget(m_action);
    connect(back, &QPushButton::clicked, this, &OrderDetailWindow::backRequested);
    connect(m_action, &QPushButton::clicked, this, [this] {
        if (m_state.actionEnabled && m_state.action != OrderListAction::None)
            emit actionRequested(m_state.businessId, m_state.type, m_state.action);
    });
}

void OrderDetailWindow::render(const OrderDetailViewState &state)
{
    m_state = state;
    m_kind->setText(state.titleText.isEmpty()
                        ? (state.type == OrderBusinessType::Charging
                               ? tr("充电订单") : tr("预约订单"))
                        : state.titleText);
    m_status->setText(state.statusText.isEmpty() ? tr("状态待确认") : state.statusText);
    m_status->setProperty("tone", state.statusTone);
    m_status->style()->unpolish(m_status);
    m_status->style()->polish(m_status);
    m_amount->setText(state.amountText.isEmpty() ? QStringLiteral("--") : state.amountText);
    m_station->setText(state.stationName.isEmpty() ? QStringLiteral("--") : state.stationName);
    m_charger->setText(state.chargerCode.isEmpty()
                           ? QStringLiteral("--") : tr("%1号桩").arg(state.chargerCode));
    m_createdAt->setText(state.createdAtText.isEmpty() ? QStringLiteral("--") : state.createdAtText);
    m_duration->setText(state.durationText.isEmpty() ? QStringLiteral("--") : state.durationText);
    m_energy->setText(state.energyText.isEmpty() ? QStringLiteral("--") : state.energyText);
    m_duration->parentWidget()->setVisible(state.type == OrderBusinessType::Charging
                                           || !state.durationText.isEmpty());
    m_energy->parentWidget()->setVisible(state.type == OrderBusinessType::Charging
                                         && !state.energyText.isEmpty());
    m_paymentMethod->setText(state.paymentMethodText.isEmpty()
                                 ? tr("钱包支付") : state.paymentMethodText);
    m_orderId->setText(state.businessId.isEmpty() ? QStringLiteral("--") : state.businessId);
    m_message->setText(state.message);
    m_message->setVisible(!state.message.isEmpty());
    m_action->setText(state.actionText);
    m_action->setEnabled(state.actionEnabled);
    m_action->setVisible(state.action != OrderListAction::None
                         && !state.actionText.isEmpty());
}
