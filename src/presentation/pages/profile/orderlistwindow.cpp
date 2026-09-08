#include "orderlistwindow.h"
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

OrderListWindow::OrderListWindow(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("orderListPage"));
    setStyleSheet(
        "#orderListPage{background:#F6F8FC;color:#0A1835;}"
        "#backButton{border:none;background:transparent;font-size:28px;color:#13223F;}"
        "QPushButton[filter='true']{border:none;border-radius:17px;background:#EDF2F9;color:#68768C;font-weight:600;}"
        "QPushButton[filter='true']:checked{background:#0874F9;color:white;}"
        "#orderCard{background:white;border:1px solid #E1E8F2;border-radius:16px;}"
        "#orderKind{color:#7B879B;font-size:12px;} #orderStation{font-size:15px;font-weight:700;}"
        "#orderMeta{color:#7B879B;font-size:12px;} #orderSummary{color:#52627A;font-size:13px;}"
        "#orderAmount{font-size:18px;font-weight:700;}"
        "#orderStatus{border-radius:10px;padding:4px 8px;background:#EAF3FF;color:#0874F9;font-weight:600;}"
        "#orderStatus[tone='warning']{background:#FFF3DC;color:#B46A00;}"
        "#orderStatus[tone='success']{background:#E8F8F1;color:#138A5B;}"
        "#orderStatus[tone='neutral']{background:#EEF1F5;color:#667085;}"
        "#orderAction{border:1px solid #0874F9;border-radius:10px;background:white;color:#0874F9;font-weight:600;padding:7px 12px;}"
        "#orderAction[primary='true']{background:#0874F9;color:white;}");
    auto *root = new QVBoxLayout(this); root->setContentsMargins(12,10,12,12); root->setSpacing(10);
    auto *header = new QHBoxLayout;
    auto *back = new QPushButton(QStringLiteral("‹"), this); back->setObjectName("backButton"); back->setFixedSize(38,38);
    auto *title = new QLabel(tr("我的订单"), this); title->setAlignment(Qt::AlignCenter); title->setStyleSheet("font-size:20px;font-weight:700;");
    auto *placeholder = new QWidget(this); placeholder->setFixedSize(38,38);
    header->addWidget(back); header->addWidget(title,1); header->addWidget(placeholder); root->addLayout(header);
    auto *filters = new QHBoxLayout; filters->setSpacing(8);
    m_allButton = new QPushButton(tr("全部"), this); m_chargingButton = new QPushButton(tr("充电订单"), this); m_reservationButton = new QPushButton(tr("预约订单"), this);
    for (QPushButton *button : {m_allButton,m_chargingButton,m_reservationButton}) { button->setProperty("filter", true); button->setCheckable(true); button->setMinimumHeight(36); filters->addWidget(button); }
    m_allButton->setChecked(true); root->addLayout(filters);
    auto *scroll = new QScrollArea(this); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *host = new QWidget(scroll); m_cardsLayout = new QVBoxLayout(host); m_cardsLayout->setContentsMargins(0,2,0,2); m_cardsLayout->setSpacing(10);
    m_emptyLabel = new QLabel(tr("暂无订单"), host); m_emptyLabel->setAlignment(Qt::AlignCenter); m_emptyLabel->setStyleSheet("color:#8A96A8;padding:48px;"); m_cardsLayout->addWidget(m_emptyLabel); m_cardsLayout->addStretch();
    scroll->setWidget(host); root->addWidget(scroll,1);
    connect(back,&QPushButton::clicked,this,&OrderListWindow::backRequested);
    connect(m_allButton,&QPushButton::clicked,this,[this]{setFilter(Filter::All);});
    connect(m_chargingButton,&QPushButton::clicked,this,[this]{setFilter(Filter::Charging);});
    connect(m_reservationButton,&QPushButton::clicked,this,[this]{setFilter(Filter::Reservation);});
}

void OrderListWindow::render(const OrderListViewState &state) { m_state=state; rebuild(); }
void OrderListWindow::setFilter(Filter filter)
{
    m_filter=filter; m_allButton->setChecked(filter==Filter::All); m_chargingButton->setChecked(filter==Filter::Charging); m_reservationButton->setChecked(filter==Filter::Reservation); rebuild();
}
void OrderListWindow::rebuild()
{
    while (m_cardsLayout->count()>0) { QLayoutItem *item=m_cardsLayout->takeAt(0); if(item->widget()) item->widget()->deleteLater(); delete item; }
    int shown=0;
    for (const OrderListItemView &order : m_state.orders) {
        if (m_filter==Filter::Charging && order.type!=OrderBusinessType::Charging) continue;
        if (m_filter==Filter::Reservation && order.type!=OrderBusinessType::Reservation) continue;
        ++shown; auto *card=new QFrame; card->setObjectName("orderCard"); auto *box=new QVBoxLayout(card); box->setContentsMargins(14,12,14,12); box->setSpacing(7);
        auto *top=new QHBoxLayout; auto *kind=new QLabel(order.type==OrderBusinessType::Charging?tr("充电订单"):tr("预约订单"),card); kind->setObjectName("orderKind"); auto *status=new QLabel(order.statusText,card); status->setObjectName("orderStatus"); status->setProperty("tone",order.statusTone); top->addWidget(kind); top->addStretch(); top->addWidget(status); box->addLayout(top);
        auto *station=new QLabel(order.stationName,card); station->setObjectName("orderStation"); station->setWordWrap(true); box->addWidget(station);
        auto *meta=new QLabel(tr("%1号桩  ·  %2").arg(order.chargerCode,order.createdAtText),card); meta->setObjectName("orderMeta"); box->addWidget(meta);
        auto *summary=new QLabel(order.summaryText,card); summary->setObjectName("orderSummary"); summary->setWordWrap(true); box->addWidget(summary);
        auto *bottom=new QHBoxLayout; auto *amount=new QLabel(order.amountText,card); amount->setObjectName("orderAmount"); bottom->addWidget(amount); bottom->addStretch();
        auto *details=new QPushButton(tr("查看详情"),card); details->setObjectName("orderAction"); connect(details,&QPushButton::clicked,this,[this,order]{emit orderActionRequested(order.businessId,order.type,OrderListAction::ViewDetails);}); bottom->addWidget(details);
        if(order.action!=OrderListAction::None && order.action!=OrderListAction::ViewDetails){auto *action=new QPushButton(order.actionText,card); action->setObjectName("orderAction"); action->setProperty("primary",order.action==OrderListAction::ContinuePayment); connect(action,&QPushButton::clicked,this,[this,order]{emit orderActionRequested(order.businessId,order.type,order.action);}); bottom->addWidget(action);} box->addLayout(bottom); m_cardsLayout->addWidget(card);
    }
    m_emptyLabel = new QLabel(tr("暂无订单")); m_emptyLabel->setAlignment(Qt::AlignCenter); m_emptyLabel->setStyleSheet("color:#8A96A8;padding:48px;"); m_cardsLayout->addWidget(m_emptyLabel);
    m_cardsLayout->addStretch();
    m_emptyLabel->setVisible(shown==0);
}
