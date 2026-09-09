#include "chargingsessionwindow.h"
#include "ui_chargingsessionwindow.h"

#include <QPixmap>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QStyle>
#include <QVBoxLayout>

namespace {
QString textOr(const QString &value, const QString &fallback)
{
    return value.isEmpty() ? fallback : value;
}
}

ChargingSessionWindow::ChargingSessionWindow(QWidget *parent)
    : QWidget(parent), ui(new Ui::ChargingSessionWindow)
{
    ui->setupUi(this);
    m_backButton = new QPushButton(QStringLiteral("‹"), this);
    m_backButton->setObjectName(QStringLiteral("sessionBackButton"));
    m_backButton->setFixedSize(42, 34);
    m_backButton->setStyleSheet(QStringLiteral(
        "#sessionBackButton{border:none;background:transparent;color:#13223F;font-size:28px;}"));
    ui->rootLayout->insertWidget(0, m_backButton, 0, Qt::AlignLeft);
    connect(m_backButton, &QPushButton::clicked, this,
            &ChargingSessionWindow::backRequested);
    m_emptyState = new QFrame(this);
    m_emptyState->setObjectName(QStringLiteral("chargingEmptyState"));
    m_emptyState->setStyleSheet(QStringLiteral(
        "#chargingEmptyState{background:white;border:1px solid #E4EAF2;border-radius:22px;}"
        "#emptyIconHalo{background:#EAF3FF;border:none;border-radius:58px;}"
        "#emptyTitle{background:transparent;color:#13223F;font-size:22px;font-weight:700;}"
        "#emptyDescription{background:transparent;color:#6F7C91;font-size:14px;}"
        "#emptyTip{background:#F3F7FD;border:none;border-radius:12px;color:#5E6F88;font-size:12px;padding:9px;}"));
    auto *emptyLayout = new QVBoxLayout(m_emptyState);
    emptyLayout->setContentsMargins(24, 28, 24, 24);
    emptyLayout->setSpacing(12);
    emptyLayout->setAlignment(Qt::AlignCenter);
    auto *iconHalo = new QFrame(m_emptyState);
    iconHalo->setObjectName(QStringLiteral("emptyIconHalo"));
    iconHalo->setFixedSize(116, 116);
    auto *iconLayout = new QVBoxLayout(iconHalo);
    iconLayout->setContentsMargins(26, 26, 26, 26);
    auto *icon = new QLabel(iconHalo);
    icon->setAlignment(Qt::AlignCenter);
    icon->setPixmap(QPixmap(QStringLiteral(":/icons/station_charge_active.png"))
                        .scaled(64, 64, Qt::KeepAspectRatio,
                                Qt::SmoothTransformation));
    iconLayout->addWidget(icon);
    auto *iconRow = new QHBoxLayout;
    iconRow->addStretch();
    iconRow->addWidget(iconHalo);
    iconRow->addStretch();
    emptyLayout->addLayout(iconRow);
    auto *emptyTitle = new QLabel(tr("还没有充电任务"), m_emptyState);
    emptyTitle->setObjectName(QStringLiteral("emptyTitle"));
    emptyTitle->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyTitle);
    auto *description = new QLabel(
        tr("扫描充电桩二维码，即可开始充电"), m_emptyState);
    description->setObjectName(QStringLiteral("emptyDescription"));
    description->setAlignment(Qt::AlignCenter);
    description->setWordWrap(true);
    emptyLayout->addWidget(description);
    auto *tip = new QLabel(
        tr("开始后可在这里查看充电进度"), m_emptyState);
    tip->setObjectName(QStringLiteral("emptyTip"));
    tip->setAlignment(Qt::AlignCenter);
    tip->setWordWrap(true);
    emptyLayout->addWidget(tip);
    ui->rootLayout->insertWidget(2, m_emptyState);
    m_progressAnimation = new QPropertyAnimation(ui->chargingProgressRing,
                                                 "displayedProgress", this);
    m_progressAnimation->setDuration(420);
    m_progressAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(ui->sessionRefreshButton, &QPushButton::clicked,
            this, &ChargingSessionWindow::refreshRequested);
    connect(ui->scanChargingButton, &QPushButton::clicked,
            this, &ChargingSessionWindow::scanChargingRequested);
    connect(ui->stopChargingButton, &QPushButton::clicked, this, [this] {
        if (m_state.status == ChargingSessionStatus::ResultUnknown)
            emit recoverStopResultRequested();
        else if (m_state.canStop)
            emit stopChargingRequested();
    });
    render(ChargingSessionViewState{});
}

ChargingSessionWindow::~ChargingSessionWindow() { delete ui; }

void ChargingSessionWindow::setEmbeddedMode(bool embedded)
{
    m_backButton->setVisible(!embedded);
}

void ChargingSessionWindow::render(const ChargingSessionViewState &state)
{
    m_state = state;
    const bool hasSession = !state.orderId.isEmpty();
    const bool charging = state.status == ChargingSessionStatus::Charging;
    const bool loading = state.status == ChargingSessionStatus::Loading;
    const bool stopping = state.status == ChargingSessionStatus::Stopping;
    const bool unknown = state.status == ChargingSessionStatus::ResultUnknown;
    const bool ended = state.status == ChargingSessionStatus::Ended;
    ui->chargingSessionTitle->setText(!hasSession ? tr("充电")
                                                : charging ? tr("正在充电")
                                                : stopping ? tr("正在结束")
                                                : unknown ? tr("确认充电结果")
                                                : ended ? tr("充电已结束")
                                                          : tr("充电进行"));
    ui->progressContainer->setVisible(hasSession);
    m_emptyState->setVisible(!hasSession && !loading);
    for (QWidget *metric : {static_cast<QWidget *>(ui->energyCaption),
                            static_cast<QWidget *>(ui->energyValueLabel),
                            static_cast<QWidget *>(ui->durationCaption),
                            static_cast<QWidget *>(ui->durationValueLabel),
                            static_cast<QWidget *>(ui->amountCaption),
                            static_cast<QWidget *>(ui->amountValueLabel),
                            static_cast<QWidget *>(ui->line1),
                            static_cast<QWidget *>(ui->line2)})
        metric->setVisible(hasSession);
    ui->chargingProgressRing->setIndeterminate(state.progressPercent < 0);
    ui->chargingProgressRing->setActive(charging || loading || stopping || unknown);
    animateProgress(state.progressPercent < 0 ? 0 : state.progressPercent);
    ui->chargingStateInRingLabel->setText(charging ? tr("正在充电")
                                                    : stopping ? tr("正在结束")
                                                    : unknown ? tr("确认结果")
                                                    : ended ? tr("已结束")
                                                              : tr("充电中"));
    ui->energyValueLabel->setText(textOr(state.energyText, tr("-- kWh")));
    ui->durationValueLabel->setText(textOr(state.durationText, tr("--")));
    ui->amountValueLabel->setText(textOr(state.amountText, tr("--")));
    const QString message = state.message;
    ui->sessionStateLabel->setText(message);
    ui->sessionStateLabel->setVisible(!message.isEmpty());
    ui->sessionRefreshButton->setVisible(state.canRefresh);
    ui->stopChargingButton->setVisible(hasSession);
    ui->stopChargingButton->setEnabled(state.canStop || state.canRecoverResult);
    ui->stopChargingButton->setText(stopping ? tr("正在结束…")
                                              : unknown ? tr("确认结束结果")
                                                        : tr("结束充电"));
    // 已有充电任务时不允许从会话页再次扫码；无任务状态保留启动充电入口。
    ui->scanChargingButton->setVisible(!hasSession);
    ui->scanChargingButton->setText(tr("扫码开始充电"));
}

void ChargingSessionWindow::renderSessions(const ChargingSessionCollectionViewState &state)
{
    // 多站点切换功能已移除：仅保存集合状态以保持外部绑定管线兼容。
    m_collection = state;
}

void ChargingSessionWindow::animateProgress(int progress)
{
    m_progressAnimation->stop();
    m_progressAnimation->setStartValue(ui->chargingProgressRing->displayedProgress());
    m_progressAnimation->setEndValue(qBound(0, progress, 100));
    m_progressAnimation->start();
}
