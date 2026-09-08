#include "reservationconfirmationwindow.h"
#include "dragscrollhelper.h"
#include "ui_reservationconfirmationwindow.h"

#include <QPushButton>
#include <QStyle>

namespace {
QString shown(const QString &value, const QString &fallback)
{
    return value.isEmpty() ? fallback : value;
}
}

ReservationConfirmationWindow::ReservationConfirmationWindow(QWidget *parent)
    : QWidget(parent), ui(new Ui::ReservationConfirmationWindow)
{
    ui->setupUi(this);
    DragScrollHelper::enableFor(this);
    connect(ui->backButton, &QPushButton::clicked, this,
            &ReservationConfirmationWindow::backRequested);
    connect(ui->reservationRetryButton, &QPushButton::clicked, this,
            &ReservationConfirmationWindow::reservationRefreshRequested);
    connect(ui->reserveButton, &QPushButton::clicked, this, [this] {
        if (m_state.status == ReservationConfirmationStatus::Ready
            && m_state.canReserve && !m_state.stationId.isEmpty()
            && !m_state.chargerId.isEmpty() && m_state.durationSeconds > 0)
            emit reserveRequested(m_state.stationId, m_state.chargerId,
                                  m_state.durationSeconds);
    });
    render(ReservationConfirmationViewState{});
}

ReservationConfirmationWindow::~ReservationConfirmationWindow() { delete ui; }

void ReservationConfirmationWindow::render(const ReservationConfirmationViewState &state)
{
    m_state = state;
    ui->stationNameLabel->setText(shown(state.stationName, tr("站点信息待加载")));
    ui->stationAddressLabel->setText(shown(state.stationAddress, tr("地址待加载")));
    ui->chargerCodeLabel->setText(shown(state.chargerCode, tr("--")));
    ui->chargerTypeLabel->setText(shown(state.chargerTypeText, tr("类型待加载")));
    ui->chargerPowerLabel->setText(shown(state.powerText, tr("-- kW")));
    ui->depositLabel->setText(shown(state.depositText, tr("--")));
    ui->durationLabel->setText(shown(state.durationText, tr("--")));
    ui->depositPolicyLabel->setText(
        shown(state.depositPolicyText,
              tr("⚠ 超过预约时限仍未开始充电，将按规则扣除预约押金")));
    const bool busy = state.status == ReservationConfirmationStatus::Loading
                      || state.status == ReservationConfirmationStatus::Submitting
                      || state.status == ReservationConfirmationStatus::ResultUnknown;
    QString message = state.message;
    if (message.isEmpty() && state.status == ReservationConfirmationStatus::Loading)
        message = tr("正在核对预约信息…");
    else if (message.isEmpty() && state.status == ReservationConfirmationStatus::Submitting)
        message = tr("正在提交预约…");
    else if (message.isEmpty() && state.status == ReservationConfirmationStatus::ResultUnknown)
        message = tr("预约结果正在确认，请勿重复提交");
    else if (message.isEmpty() && !state.canReserve)
        message = state.disabledReason;
    ui->reservationStateLabel->setText(message);
    ui->reservationStateLabel->setVisible(!message.isEmpty());
    ui->reservationRetryButton->setVisible(state.canRetry && !busy);
    ui->reserveButton->setEnabled(state.status == ReservationConfirmationStatus::Ready
                                  && state.canReserve && state.durationSeconds > 0);
    ui->reserveButton->setText(state.status == ReservationConfirmationStatus::Submitting
                                   ? tr("正在预约…") : tr("确认预约"));
    ui->reserveButton->setToolTip(ui->reserveButton->isEnabled()
                                      ? QString() : state.disabledReason);
}
