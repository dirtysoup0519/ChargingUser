#include "frequentstationswindow.h"

#include "dragscrollhelper.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

FrequentStationsWindow::FrequentStationsWindow(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("frequentStationsPage"));
    setStyleSheet(QStringLiteral(
        "#frequentStationsPage{background:#F8FAFE;color:#0A1835;}"
        "#backButton{border:none;background:transparent;color:#13223F;font-size:28px;}"
        "#frequentStationCard{background:white;border:1px solid #E4EAF2;border-radius:16px;}"
        "#stationRank{background:#EAF3FF;border-radius:18px;color:#0874F9;font-size:16px;font-weight:700;}"
        "#stationName{color:#13223F;font-size:15px;font-weight:700;}"
        "#stationMeta{color:#748096;font-size:12px;}"
        "#stationOpen{border:1px solid #0874F9;border-radius:10px;background:white;color:#0874F9;font-weight:600;padding:7px 10px;}"));
    DragScrollHelper::enableFor(this);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 14);
    root->setSpacing(10);
    auto *header = new QHBoxLayout;
    auto *back = new QPushButton(QStringLiteral("‹"), this);
    back->setObjectName(QStringLiteral("backButton"));
    back->setFixedSize(38, 38);
    auto *title = new QLabel(tr("常用充电站"), this);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral("font-size:21px;font-weight:700;"));
    auto *placeholder = new QWidget(this);
    placeholder->setFixedSize(38, 38);
    header->addWidget(back);
    header->addWidget(title, 1);
    header->addWidget(placeholder);
    root->addLayout(header);
    auto *hint = new QLabel(tr("根据历史订单中的充电站使用次数生成"), this);
    hint->setAlignment(Qt::AlignCenter);
    hint->setStyleSheet(QStringLiteral("color:#7B879B;font-size:13px;"));
    root->addWidget(hint);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *host = new QWidget(scroll);
    m_cards = new QVBoxLayout(host);
    m_cards->setContentsMargins(0, 2, 0, 2);
    m_cards->setSpacing(10);
    scroll->setWidget(host);
    root->addWidget(scroll, 1);
    connect(back, &QPushButton::clicked, this, &FrequentStationsWindow::backRequested);
}

void FrequentStationsWindow::render(const FrequentStationsViewState &state)
{
    while (m_cards->count() > 0) {
        QLayoutItem *item = m_cards->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    int rank = 0;
    for (const FrequentStationItemView &station : state.stations) {
        ++rank;
        auto *card = new QFrame;
        card->setObjectName(QStringLiteral("frequentStationCard"));
        auto *layout = new QHBoxLayout(card);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(10);
        auto *rankLabel = new QLabel(QString::number(rank), card);
        rankLabel->setObjectName(QStringLiteral("stationRank"));
        rankLabel->setFixedSize(36, 36);
        rankLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(rankLabel);
        auto *text = new QWidget(card);
        auto *textLayout = new QVBoxLayout(text);
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(4);
        auto *name = new QLabel(station.stationName, text);
        name->setObjectName(QStringLiteral("stationName"));
        name->setWordWrap(true);
        auto *meta = new QLabel(
            tr("累计 %1 笔订单 · 最近使用 %2")
                .arg(station.orderCount)
                .arg(station.lastUsedText.isEmpty() ? tr("--") : station.lastUsedText), text);
        meta->setObjectName(QStringLiteral("stationMeta"));
        meta->setWordWrap(true);
        textLayout->addWidget(name);
        textLayout->addWidget(meta);
        layout->addWidget(text, 1);
        auto *open = new QPushButton(tr("查看站点"), card);
        open->setObjectName(QStringLiteral("stationOpen"));
        open->setEnabled(!station.stationId.isEmpty());
        connect(open, &QPushButton::clicked, this, [this, station] {
            if (!station.stationId.isEmpty()) emit stationRequested(station.stationId);
        });
        layout->addWidget(open);
        m_cards->addWidget(card);
    }
    m_message = new QLabel(state.message.isEmpty() ? tr("暂无常用充电站") : state.message);
    m_message->setAlignment(Qt::AlignCenter);
    m_message->setWordWrap(true);
    m_message->setStyleSheet(QStringLiteral("color:#8A96A8;padding:52px 20px;"));
    m_message->setVisible(state.stations.isEmpty());
    m_cards->addWidget(m_message);
    m_cards->addStretch();
}
