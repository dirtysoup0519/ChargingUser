#include "legaldocumentpage.h"
#include "dragscrollhelper.h"

#include <QColor>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

LegalDocumentPage::LegalDocumentPage(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("legalDocumentPage"));
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);
    QPalette pagePalette = palette();
    pagePalette.setColor(QPalette::Window, QColor(QStringLiteral("#F8FAFE")));
    setPalette(pagePalette);
    setStyleSheet(QStringLiteral(
        "#legalDocumentPage{background:#F8FAFE;color:#0A1835;}"
        "#legalBackButton{border:none;background:transparent;color:#13223F;font-size:28px;}"
        "#legalCard{background:white;border:1px solid #E4EAF2;border-radius:16px;}"
        "#legalBody{background:transparent;color:#46546C;font-size:14px;line-height:1.7;}"));
    DragScrollHelper::enableFor(this);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 10, 16, 16);
    root->setSpacing(10);
    auto *header = new QHBoxLayout;
    auto *back = new QPushButton(QStringLiteral("‹"), this);
    back->setObjectName(QStringLiteral("legalBackButton"));
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
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *card = new QFrame(scroll);
    card->setObjectName(QStringLiteral("legalCard"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(20, 20, 20, 20);
    m_body = new QLabel(card);
    m_body->setObjectName(QStringLiteral("legalBody"));
    m_body->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_body->setWordWrap(true);
    m_body->setTextFormat(Qt::PlainText);
    cardLayout->addWidget(m_body);
    cardLayout->addStretch();
    scroll->setWidget(card);
    root->addWidget(scroll, 1);
    connect(back, &QPushButton::clicked, this, &LegalDocumentPage::backRequested);
}

void LegalDocumentPage::showDocument(const QString &title, const QString &body)
{
    m_title->setText(title);
    m_body->setText(body);
}
