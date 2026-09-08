#include "profileeditwindow.h"
#include "dragscrollhelper.h"
#include "ui_profileeditwindow.h"
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>

namespace {

QString feedbackKind(SubmitState state)
{
    switch (state) {
    case SubmitState::Success: return QStringLiteral("success");
    case SubmitState::ResultUnknown: return QStringLiteral("unknown");
    case SubmitState::ValidationError:
    case SubmitState::NetworkError:
    case SubmitState::ServerError: return QStringLiteral("error");
    case SubmitState::Idle:
    case SubmitState::Loading: return QStringLiteral("neutral");
    }
    return QStringLiteral("neutral");
}

void refreshStyle(QWidget *widget)
{
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}

} // namespace

ProfileEditWindow::ProfileEditWindow(QWidget *parent)
    : QWidget(parent), ui(new Ui::ProfileEditWindow)
{
    ui->setupUi(this);
    DragScrollHelper::enableFor(this);
    m_newPasswordLabel = new QLabel(tr("设置密码"), ui->profileCard);
    m_newPasswordEdit = new QLineEdit(ui->profileCard);
    m_newPasswordEdit->setObjectName(QStringLiteral("newPasswordEdit"));
    m_newPasswordEdit->setMinimumHeight(46);
    m_newPasswordEdit->setEchoMode(QLineEdit::Password);
    m_newPasswordEdit->setPlaceholderText(tr("请输入至少 6 位新密码"));
    m_confirmPasswordLabel = new QLabel(tr("确认密码"), ui->profileCard);
    m_confirmPasswordEdit = new QLineEdit(ui->profileCard);
    m_confirmPasswordEdit->setObjectName(QStringLiteral("confirmPasswordEdit"));
    m_confirmPasswordEdit->setMinimumHeight(46);
    m_confirmPasswordEdit->setEchoMode(QLineEdit::Password);
    m_confirmPasswordEdit->setPlaceholderText(tr("请再次输入新密码"));
    m_changePasswordButton = new QPushButton(tr("修改密码"), ui->profileCard);
    m_changePasswordButton->setObjectName(QStringLiteral("changePasswordButton"));
    m_changePasswordButton->setMinimumHeight(42);
    ui->formLayout->addWidget(m_newPasswordLabel);
    ui->formLayout->addWidget(m_newPasswordEdit);
    ui->formLayout->addWidget(m_confirmPasswordLabel);
    ui->formLayout->addWidget(m_confirmPasswordEdit);
    ui->formLayout->addWidget(m_changePasswordButton);
    connect(ui->backButton, &QPushButton::clicked,
            this, &ProfileEditWindow::backRequested);
    connect(ui->saveButton, &QPushButton::clicked,
            this, &ProfileEditWindow::submitCurrentInput);
    connect(ui->nicknameEdit, &QLineEdit::returnPressed,
            this, &ProfileEditWindow::submitCurrentInput);
    connect(m_confirmPasswordEdit, &QLineEdit::returnPressed,
            this, &ProfileEditWindow::submitCurrentInput);
    connect(m_changePasswordButton, &QPushButton::clicked,
            this, &ProfileEditWindow::passwordChangeRequested);
    setEditMode(ProfileEditMode::ExistingProfile);
    render(ProfileEditViewState{});
}

ProfileEditWindow::~ProfileEditWindow() { delete ui; }

void ProfileEditWindow::setEditMode(ProfileEditMode mode, const QString &username)
{
    m_mode = mode;
    const bool phoneSetup = mode == ProfileEditMode::PhoneFirstSetup;
    const bool usernameSetup = mode == ProfileEditMode::UsernameFirstSetup;
    const bool existing = mode == ProfileEditMode::ExistingProfile;
    ui->pageTitle->setText(existing ? tr("个人资料编辑") : tr("完善个人资料"));
    ui->nicknameLabel->setText(usernameSetup ? tr("用户名") : tr("昵称"));
    if (usernameSetup) ui->nicknameEdit->setText(username);
    if (usernameSetup) ui->phoneEdit->clear();
    m_newPasswordEdit->clear();
    m_confirmPasswordEdit->clear();
    ui->nicknameEdit->setReadOnly(usernameSetup);
    ui->phoneEdit->setReadOnly(!usernameSetup);
    ui->phoneEdit->setPlaceholderText(usernameSetup ? tr("请输入需要绑定的手机号") : QString());
    ui->hintLabel2->setText(usernameSetup
                                ? tr("首次使用用户名登录必须绑定手机号")
                                : tr("手机号码来自登录账号，暂不支持在此修改"));
    m_newPasswordLabel->setVisible(phoneSetup);
    m_newPasswordEdit->setVisible(phoneSetup);
    m_confirmPasswordLabel->setVisible(phoneSetup);
    m_confirmPasswordEdit->setVisible(phoneSetup);
    m_changePasswordButton->setVisible(existing);
    ui->avatarLabel->setVisible(existing);
    ui->avatarButton->setVisible(existing);
}

void ProfileEditWindow::render(const ProfileEditViewState &state)
{
    if (!state.phone.isEmpty() && ui->phoneEdit->text() != state.phone) {
        const QSignalBlocker blocker(ui->phoneEdit);
        ui->phoneEdit->setText(state.phone);
    }
    if (!state.nicknameInput.isNull() && !ui->nicknameEdit->hasFocus()
        && ui->nicknameEdit->text() != state.nicknameInput) {
        const QSignalBlocker blocker(ui->nicknameEdit);
        ui->nicknameEdit->setText(state.nicknameInput);
    }

    const bool loading = state.submitState == SubmitState::Loading;
    ui->saveButton->setEnabled(state.canSubmit && !loading);
    ui->saveButton->setText(loading ? tr("保存中…") : tr("保存"));
    ui->loadingIndicator->setVisible(loading);

    QString message = state.message;
    if (state.submitState == SubmitState::ResultUnknown && message.isEmpty())
        message = tr("保存结果暂时未知，请勿重复提交，稍后刷新资料确认。");
    ui->errorLabel->setText(message);
    ui->errorLabel->setVisible(!message.isEmpty());
    ui->errorLabel->setProperty("feedbackKind", feedbackKind(state.submitState));
    refreshStyle(ui->errorLabel);
}

void ProfileEditWindow::submitCurrentInput()
{
    if (!ui->saveButton->isEnabled())
        return;
    const QString nickname = ui->nicknameEdit->text().trimmed();
    if (m_mode == ProfileEditMode::ExistingProfile) {
        emit profileSaveRequested(nickname);
        return;
    }
    const QString phone = ui->phoneEdit->text().trimmed();
    const QString password = m_newPasswordEdit->text();
    if (m_mode == ProfileEditMode::UsernameFirstSetup
        && (phone.size() != 11 || !phone.startsWith(QLatin1Char('1')))) {
        ui->errorLabel->setText(tr("请输入正确的 11 位绑定手机号"));
        ui->errorLabel->show();
        return;
    }
    if (m_mode == ProfileEditMode::PhoneFirstSetup
        && (password.size() < 6 || password != m_confirmPasswordEdit->text())) {
        ui->errorLabel->setText(password.size() < 6
                                    ? tr("新密码至少需要 6 位")
                                    : tr("两次输入的密码不一致"));
        ui->errorLabel->show();
        return;
    }
    emit profileCompletionRequested(nickname, phone, password);
}
