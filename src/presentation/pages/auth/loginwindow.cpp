#include "loginwindow.h"
#include "dragscrollhelper.h"
#include "ui_loginwindow.h"
#include <QCheckBox>
#include <QLineEdit>
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

LoginWindow::LoginWindow(QWidget *parent) : QWidget(parent), ui(new Ui::LoginWindow)
{
    ui->setupUi(this);
    DragScrollHelper::enableFor(this);
    connect(ui->btnLogin, &QPushButton::clicked,
            this, &LoginWindow::submitCurrentInput);
    connect(ui->editPhoneNumber, &QLineEdit::returnPressed,
            this, &LoginWindow::submitCurrentInput);
    connect(ui->agreementCheck, &QCheckBox::toggled, this, [this](bool checked) {
        ui->agreementCheck->setProperty("validationError", false);
        refreshStyle(ui->agreementCheck);
        if (checked && ui->errorLabel->property("agreementValidation").toBool()) {
            ui->errorLabel->clear();
            ui->errorLabel->hide();
            ui->errorLabel->setProperty("agreementValidation", false);
        }
    });
    render(LoginViewState{});
}

LoginWindow::~LoginWindow() { delete ui; }

void LoginWindow::render(const LoginViewState &state)
{
    const bool explicitlyCleared = !state.phoneInput.isNull()
                                   && state.phoneInput.isEmpty();
    if (!state.phoneInput.isNull()
        && (explicitlyCleared || !ui->editPhoneNumber->hasFocus())
        && ui->editPhoneNumber->text() != state.phoneInput) {
        const QSignalBlocker blocker(ui->editPhoneNumber);
        ui->editPhoneNumber->setText(state.phoneInput);
    }

    const bool loading = state.submitState == SubmitState::Loading;
    ui->btnLogin->setEnabled(state.canSubmit && !loading);
    ui->btnLogin->setText(loading ? tr("登录中…") : tr("登录"));
    ui->loadingIndicator->setVisible(loading);

    QString message = state.message;
    if (state.submitState == SubmitState::ResultUnknown && message.isEmpty())
        message = tr("登录结果暂时未知，请稍后重试。");
    ui->errorLabel->setText(message);
    ui->errorLabel->setVisible(!message.isEmpty());
    ui->errorLabel->setProperty("agreementValidation", false);
    ui->errorLabel->setProperty("feedbackKind", feedbackKind(state.submitState));
    refreshStyle(ui->errorLabel);
}

void LoginWindow::submitCurrentInput()
{
    if (!ui->btnLogin->isEnabled())
        return;
    if (!ui->agreementCheck->isChecked()) {
        ui->agreementCheck->setProperty("validationError", true);
        refreshStyle(ui->agreementCheck);
        ui->errorLabel->setText(tr("请先阅读并同意《用户协议》和《隐私政策》"));
        ui->errorLabel->setProperty("agreementValidation", true);
        ui->errorLabel->setProperty("feedbackKind", QStringLiteral("error"));
        ui->errorLabel->show();
        refreshStyle(ui->errorLabel);
        return;
    }
    emit loginRequested(ui->editPhoneNumber->text().trimmed());
}
