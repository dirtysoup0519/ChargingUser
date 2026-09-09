#include "chargeconfirmationwindow.h"
#include "dragscrollhelper.h"
#include "ui_chargeconfirmationwindow.h"

#include <QPushButton>
#include <QStyle>

namespace {
QString textOr(const QString &text, const QString &fallback)
{
    return text.isEmpty() ? fallback : text;
}

void refreshStyle(QWidget *widget)
{
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}
}

ChargeConfirmationWindow::ChargeConfirmationWindow(QWidget *parent)
    : QWidget(parent), ui(new Ui::ChargeConfirmationWindow)
{
    ui->setupUi(this);
    DragScrollHelper::enableFor(this);
    connect(ui->backButton, &QPushButton::clicked, this, &ChargeConfirmationWindow::backRequested);
    connect(ui->confirmationRetryButton, &QPushButton::clicked,
            this, &ChargeConfirmationWindow::confirmationRefreshRequested);
    connect(ui->rechargeButton, &QPushButton::clicked,
            this, &ChargeConfirmationWindow::rechargeRequested);
    connect(ui->startChargingButton, &QPushButton::clicked, this, [this] {
        if (m_state.status == ChargeConfirmationStatus::Ready && m_state.canStart
            && !m_state.stationId.isEmpty() && !m_state.chargerId.isEmpty())
            emit startChargingRequested(m_state.stationId, m_state.chargerId);
    });
    render(ChargeConfirmationViewState{});
}

ChargeConfirmationWindow::~ChargeConfirmationWindow() { delete ui; }

void ChargeConfirmationWindow::render(const ChargeConfirmationViewState &state)
{
    m_state = state;
    ui->stationNameLabel->setText(textOr(state.stationName, tr("站点信息待加载")));
    ui->stationAddressLabel->setText(textOr(state.stationAddress, tr("地址待加载")));
    ui->stationDistanceLabel->clear();
    ui->chargerCodeLabel->setText(textOr(state.chargerCode, tr("--")));
    ui->chargerTypeLabel->setText(textOr(state.chargerTypeText, tr("类型待加载")));
    ui->chargerPowerLabel->setText(textOr(state.powerText, tr("-- kW")));
    ui->chargerStatusLabel->setText(textOr(state.chargerStatusText, tr("状态待加载")));
    ui->energyPriceLabel->setText(textOr(state.energyPriceText, tr("--")));
    ui->walletBalanceLabel->setText(textOr(state.walletBalanceText, tr("--")));

    const bool loading = state.status == ChargeConfirmationStatus::Loading;
    const bool submitting = state.status == ChargeConfirmationStatus::Submitting;
    const bool unknown = state.status == ChargeConfirmationStatus::ResultUnknown;
    QString message = state.message;
    if (message.isEmpty()) {
        if (loading) message = tr("正在核对充电信息…");
        else if (submitting) message = tr("正在启动充电，请稍候…");
        else if (unknown) message = tr("启动结果正在确认，请勿重复操作");
        else if (!state.canStart && !state.disabledReason.isEmpty()) message = state.disabledReason;
    }
    ui->confirmationStateLabel->setText(message);
    ui->confirmationStateLabel->setVisible(!message.isEmpty());
    ui->confirmationStateLabel->setProperty("state", state.status == ChargeConfirmationStatus::Error
                                                         ? "error" : unknown ? "unknown" : "neutral");
    refreshStyle(ui->confirmationStateLabel);
    ui->confirmationRetryButton->setVisible(state.canRetry && !loading && !submitting && !unknown);
    ui->rechargeButton->setVisible(state.canRecharge);
    ui->rechargeButton->setEnabled(state.canRecharge && !submitting && !unknown);
    ui->startChargingButton->setEnabled(state.status == ChargeConfirmationStatus::Ready
                                         && state.canStart && !state.stationId.isEmpty()
                                         && !state.chargerId.isEmpty());
    ui->startChargingButton->setText(submitting ? tr("正在启动…")
                                                : unknown ? tr("正在确认结果…")
                                                          : tr("确认开始充电"));
    ui->startChargingButton->setToolTip(ui->startChargingButton->isEnabled()
                                            ? QString() : state.disabledReason);
}
