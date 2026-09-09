#include "passwordchangewindow.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

PasswordChangeWindow::PasswordChangeWindow(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("passwordChangePage"));
    setStyleSheet(QStringLiteral(
        "#passwordChangePage{background:#F8FAFE;color:#0A1835;}"
        "#backButton{border:none;background:transparent;color:#13223F;font-size:28px;}"
        "#passwordCard{background:white;border:1px solid #E4EAF2;border-radius:16px;}"
        "QLineEdit{min-height:46px;border:1px solid #D8E2F0;border-radius:12px;padding:0 12px;background:#F8FAFE;}"
        "#passwordSubmit{min-height:50px;border:none;border-radius:14px;background:#0874F9;color:white;font-size:17px;font-weight:700;}"
        "#passwordMessage{color:#D14343;font-size:13px;}"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 10, 16, 18);
    root->setSpacing(12);
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
    m_instruction = new QLabel(this);
    m_instruction->setWordWrap(true);
    m_instruction->setStyleSheet(QStringLiteral("color:#6F7C91;font-size:14px;padding:6px 2px;"));
    root->addWidget(m_instruction);
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("passwordCard"));
    auto *form = new QVBoxLayout(card);
    form->setContentsMargins(16, 18, 16, 18);
    form->setSpacing(9);
    m_originalLabel = new QLabel(tr("原密码"), card);
    m_originalEdit = new QLineEdit(card);
    m_originalEdit->setEchoMode(QLineEdit::Password);
    m_originalEdit->setPlaceholderText(tr("请输入当前密码"));
    m_newLabel = new QLabel(tr("新密码"), card);
    m_newEdit = new QLineEdit(card);
    m_newEdit->setEchoMode(QLineEdit::Password);
    m_newEdit->setPlaceholderText(tr("请输入至少 6 位新密码"));
    m_confirmLabel = new QLabel(tr("确认新密码"), card);
    m_confirmEdit = new QLineEdit(card);
    m_confirmEdit->setEchoMode(QLineEdit::Password);
    m_confirmEdit->setPlaceholderText(tr("请再次输入新密码"));
    m_originalEdit->setAttribute(Qt::WA_InputMethodEnabled, true);
    m_newEdit->setAttribute(Qt::WA_InputMethodEnabled, true);
    m_confirmEdit->setAttribute(Qt::WA_InputMethodEnabled, true);
    form->addWidget(m_originalLabel);
    form->addWidget(m_originalEdit);
    form->addWidget(m_newLabel);
    form->addWidget(m_newEdit);
    form->addWidget(m_confirmLabel);
    form->addWidget(m_confirmEdit);
    root->addWidget(card);
    m_message = new QLabel(this);
    m_message->setObjectName(QStringLiteral("passwordMessage"));
    m_message->setAlignment(Qt::AlignCenter);
    m_message->setWordWrap(true);
    root->addWidget(m_message);
    root->addStretch();
    m_submit = new QPushButton(this);
    m_submit->setObjectName(QStringLiteral("passwordSubmit"));
    root->addWidget(m_submit);
    connect(back, &QPushButton::clicked, this, &PasswordChangeWindow::backRequested);
    connect(m_submit, &QPushButton::clicked, this, &PasswordChangeWindow::submit);
    connect(m_originalEdit, &QLineEdit::returnPressed, this, &PasswordChangeWindow::submit);
    connect(m_confirmEdit, &QLineEdit::returnPressed, this, &PasswordChangeWindow::submit);
    reset();
}

void PasswordChangeWindow::reset(const QString &message)
{
    m_title->setText(tr("修改密码"));
    m_instruction->setText(tr("请输入当前密码，并设置新的登录密码。"));
    m_originalLabel->setVisible(true);
    m_originalEdit->setVisible(true);
    m_newLabel->setVisible(true);
    m_newEdit->setVisible(true);
    m_confirmLabel->setVisible(true);
    m_confirmEdit->setVisible(true);
    m_originalEdit->setEnabled(true);
    m_newEdit->setEnabled(true);
    m_confirmEdit->setEnabled(true);
    m_submit->setEnabled(true);
    m_submit->setText(tr("确认修改密码"));
    m_originalEdit->clear();
    m_newEdit->clear();
    m_confirmEdit->clear();
    m_message->setText(message);
    m_message->setVisible(!message.isEmpty());
}

void PasswordChangeWindow::setSubmitting(bool submitting, const QString &message)
{
    m_originalEdit->setEnabled(!submitting);
    m_newEdit->setEnabled(!submitting);
    m_confirmEdit->setEnabled(!submitting);
    m_submit->setEnabled(!submitting);
    m_submit->setText(submitting ? tr("正在提交…") : tr("确认修改密码"));
    m_message->setText(message);
    m_message->setVisible(!message.isEmpty());
}

void PasswordChangeWindow::submit()
{
    m_message->hide();
    if (m_originalEdit->text().isEmpty()) {
        m_message->setText(tr("请输入原密码"));
        m_message->show();
        return;
    }
    if (m_newEdit->text().size() < 6) {
        m_message->setText(tr("新密码至少需要 6 位"));
        m_message->show();
        return;
    }
    if (m_newEdit->text() != m_confirmEdit->text()) {
        m_message->setText(tr("两次输入的新密码不一致"));
        m_message->show();
        return;
    }
    emit passwordSubmitted(m_originalEdit->text(), m_newEdit->text());
}
