#include "profileeditwindow.h"
#include "ui_profileeditwindow.h"
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

ProfileEditWindow::ProfileEditWindow(QWidget *parent)
    : QWidget(parent), ui(new Ui::ProfileEditWindow)
{
    ui->setupUi(this);
    setWindowFlag(Qt::FramelessWindowHint);
    connect(ui->backButton, &QPushButton::clicked,
            this, &ProfileEditWindow::backRequested);
    connect(ui->saveButton, &QPushButton::clicked,
            this, &ProfileEditWindow::submitCurrentInput);
    connect(ui->nicknameEdit, &QLineEdit::returnPressed,
            this, &ProfileEditWindow::submitCurrentInput);
    render(ProfileEditViewState{});
}

ProfileEditWindow::~ProfileEditWindow() { delete ui; }

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
    emit profileSaveRequested(ui->nicknameEdit->text().trimmed());
}
