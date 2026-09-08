#include "profiletextwindow.h"

#include "dragscrollhelper.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

ProfileTextWindow::ProfileTextWindow(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("profileTextPage"));
    setStyleSheet(QStringLiteral(
        "#profileTextPage{background:#F8FAFE;color:#0A1835;}"
        "#backButton{border:none;background:transparent;color:#13223F;font-size:28px;}"
        "#contentCard{background:white;border:1px solid #E4EAF2;border-radius:16px;}"
        "#contentBody{background:transparent;color:#46546C;font-size:14px;}"));
    DragScrollHelper::enableFor(this);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 14);
    auto *header = new QHBoxLayout;
    auto *back = new QPushButton(QStringLiteral("‹"), this);
    back->setObjectName(QStringLiteral("backButton"));
    back->setFixedSize(38, 38);
    m_title = new QLabel(this);
    m_title->setAlignment(Qt::AlignCenter);
    m_title->setStyleSheet(QStringLiteral("font-size:21px;font-weight:700;"));
    auto *placeholder = new QWidget(this);
    placeholder->setFixedSize(38, 38);
    header->addWidget(back);
    header->addWidget(m_title, 1);
    header->addWidget(placeholder);
    root->addLayout(header);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *card = new QFrame(scroll);
    card->setObjectName(QStringLiteral("contentCard"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(20, 22, 20, 22);
    m_body = new QLabel(card);
    m_body->setObjectName(QStringLiteral("contentBody"));
    m_body->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_body->setWordWrap(true);
    m_body->setTextFormat(Qt::PlainText);
    cardLayout->addWidget(m_body);
    cardLayout->addStretch();
    scroll->setWidget(card);
    root->addWidget(scroll, 1);
    connect(back, &QPushButton::clicked, this, &ProfileTextWindow::backRequested);
}

void ProfileTextWindow::renderContent(const QString &title, const QString &body)
{
    m_title->setText(title);
    m_body->setText(body);
}
