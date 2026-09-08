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
    setStep(PasswordChangeStep::VerifyOriginal);
}

void PasswordChangeWindow::setStep(PasswordChangeStep step, const QString &message)
{
    m_step = step;
    const bool verify = step == PasswordChangeStep::VerifyOriginal;
    m_title->setText(verify ? tr("验证原密码") : tr("设置新密码"));
    m_instruction->setText(verify
        ? tr("为保障账号安全，请先验证当前登录密码。")
        : tr("原密码验证成功，请设置新的登录密码。"));
    m_originalLabel->setVisible(verify);
    m_originalEdit->setVisible(verify);
    m_newLabel->setVisible(!verify);
    m_newEdit->setVisible(!verify);
    m_confirmLabel->setVisible(!verify);
    m_confirmEdit->setVisible(!verify);
    m_submit->setText(verify ? tr("验证原密码") : tr("确认修改密码"));
    m_message->setText(message);
    m_message->setVisible(!message.isEmpty());
    if (verify) m_originalEdit->clear();
    else { m_newEdit->clear(); m_confirmEdit->clear(); }
}

void PasswordChangeWindow::submit()
{
    m_message->hide();
    if (m_step == PasswordChangeStep::VerifyOriginal) {
        if (m_originalEdit->text().isEmpty()) {
            m_message->setText(tr("请输入原密码"));
            m_message->show();
            return;
        }
        emit originalPasswordSubmitted(m_originalEdit->text());
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
    emit newPasswordSubmitted(m_newEdit->text());
}
