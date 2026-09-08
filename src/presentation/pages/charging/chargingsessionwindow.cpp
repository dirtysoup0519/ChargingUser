#include "chargingsessionwindow.h"
#include "ui_chargingsessionwindow.h"

#include <QComboBox>
#include <QAbstractItemView>
#include <QPalette>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QListWidget>
#include <QListWidgetItem>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QSignalBlocker>
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
        tr("开始后可在这里查看进度，并切换多个充电任务"), m_emptyState);
    tip->setObjectName(QStringLiteral("emptyTip"));
    tip->setAlignment(Qt::AlignCenter);
    tip->setWordWrap(true);
    emptyLayout->addWidget(tip);
    ui->rootLayout->insertWidget(1, m_emptyState);
    ui->selectedStationLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    ui->selectedChargerLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    // The collapsed combo text stays transparent because the two labels render
    // the richer card. Its popup needs an independent palette or it inherits
    // that transparent foreground and appears as empty rows.
    QPalette popupPalette = ui->sessionSelectorButton->view()->palette();
    popupPalette.setColor(QPalette::Text, QColor(QStringLiteral("#13223F")));
    popupPalette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#0874F9")));
    ui->sessionSelectorButton->view()->setPalette(popupPalette);
    ui->sessionSelectorButton->view()->setTextElideMode(Qt::ElideRight);
    ui->sessionSelectorButton->view()->setHorizontalScrollBarPolicy(
        Qt::ScrollBarAlwaysOff);
    m_sessionChooserPanel = new QFrame(this);
    m_sessionChooserPanel->setObjectName(QStringLiteral("sessionChooserPanel"));
    m_sessionChooserPanel->setStyleSheet(QStringLiteral(
        "#sessionChooserPanel { background: #F8FAFE; border: 1px solid #C9D9EE; "
        "border-radius: 16px; }"
        "#sessionChooserTitle { border: none; color: #0A1835; font-size: 18px; font-weight: 700; }"
        "#sessionChooserClose { border: none; border-radius: 16px; background: #EAF1FA; "
        "color: #52627A; font-size: 18px; }"));
    auto *chooserLayout = new QVBoxLayout(m_sessionChooserPanel);
    chooserLayout->setContentsMargins(14, 12, 14, 14);
    chooserLayout->setSpacing(10);
    auto *chooserHeader = new QHBoxLayout;
    auto *chooserTitle = new QLabel(tr("选择充电桩"), m_sessionChooserPanel);
    chooserTitle->setObjectName(QStringLiteral("sessionChooserTitle"));
    auto *chooserClose = new QPushButton(QStringLiteral("×"), m_sessionChooserPanel);
    chooserClose->setObjectName(QStringLiteral("sessionChooserClose"));
    chooserClose->setFixedSize(32, 32);
    chooserHeader->addWidget(chooserTitle, 1);
    chooserHeader->addWidget(chooserClose);
    chooserLayout->addLayout(chooserHeader);
    m_sessionChooser = new QListWidget(m_sessionChooserPanel);
    m_sessionChooser->setObjectName(QStringLiteral("sessionChooser"));
    m_sessionChooser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sessionChooser->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_sessionChooser->setStyleSheet(QStringLiteral(
        "#sessionChooser { background: #FFFFFF; border: 1px solid #D5E1F2; "
        "border-radius: 12px; padding: 5px; outline: none; }"
        "#sessionChooser::item { min-height: 58px; padding: 0 12px; "
        "border-radius: 8px; color: #13223F; }"
        "#sessionChooser::item:hover { background: #F3F7FD; }"
        "#sessionChooser::item:selected { background: #EAF3FF; color: #0874F9; }"));
    chooserLayout->addWidget(m_sessionChooser, 1);
    m_sessionChooserPanel->hide();
    connect(chooserClose, &QPushButton::clicked, m_sessionChooserPanel, &QWidget::hide);
    connect(ui->sessionSelectorButton, &SessionSelectorComboBox::selectorRequested,
            this, [this] {
        if (m_sessionChooser->count() <= 0) return;
        positionSessionChooser();
        m_sessionChooserPanel->setVisible(!m_sessionChooserPanel->isVisible());
        if (m_sessionChooserPanel->isVisible()) m_sessionChooserPanel->raise();
    });
    connect(m_sessionChooser, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *item) {
        if (!item) return;
        m_sessionChooserPanel->hide();
        const QString orderId = item->data(Qt::UserRole).toString();
        if (!orderId.isEmpty() && orderId != m_collection.selectedOrderId)
            emit activeSessionSelected(orderId);
    });
    m_progressAnimation = new QPropertyAnimation(ui->chargingProgressRing,
                                                 "displayedProgress", this);
    m_progressAnimation->setDuration(420);
    m_progressAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(ui->sessionSelectorButton, QOverload<int>::of(&QComboBox::activated),
            this, [this](int index) {
        if (m_updatingSelector || index < 0) return;
        const QString orderId = ui->sessionSelectorButton->itemData(index).toString();
        if (!orderId.isEmpty() && orderId != m_collection.selectedOrderId)
            emit activeSessionSelected(orderId);
    });
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

void ChargingSessionWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    positionSessionChooser();
}

void ChargingSessionWindow::positionSessionChooser()
{
    if (!m_sessionChooserPanel) return;
    const int margin = 16;
    const int panelHeight = qMin(360, qMax(230, height() - 180));
    m_sessionChooserPanel->setGeometry(margin,
                                       (height() - panelHeight) / 2,
                                       qMax(0, width() - margin * 2),
                                       panelHeight);
}

void ChargingSessionWindow::render(const ChargingSessionViewState &state)
{
    m_state = state;
    const bool hasSession = !state.orderId.isEmpty();
    const bool charging = state.status == ChargingSessionStatus::Charging;
    const bool loading = state.status == ChargingSessionStatus::Loading;
    const bool stopping = state.status == ChargingSessionStatus::Stopping;
    const bool unknown = state.status == ChargingSessionStatus::ResultUnknown;
    ui->chargingSessionTitle->setText(!hasSession ? tr("充电")
                                                : charging ? tr("正在充电")
                                                : stopping ? tr("正在结束")
                                                : unknown ? tr("确认充电结果")
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
    ui->sessionSwitchHintLabel->setVisible(hasSession);
    ui->sessionCard->setVisible(hasSession);
    ui->chargingProgressRing->setIndeterminate(state.progressPercent < 0);
    ui->chargingProgressRing->setActive(charging || loading || stopping || unknown);
    animateProgress(state.progressPercent < 0 ? 0 : state.progressPercent);
    ui->chargingStateInRingLabel->setText(charging ? tr("正在充电")
                                                    : stopping ? tr("正在结束")
                                                    : unknown ? tr("确认结果")
                                                              : tr("充电中"));
    ui->selectedStationLabel->setText(hasSession
                                          ? textOr(state.stationName, tr("未知站点"))
                                          : tr("暂无进行中的充电"));
    ui->selectedChargerLabel->setText(hasSession
        ? tr("%1号桩  ·  %2  ·  %3  ·  充电中")
              .arg(textOr(state.chargerCode, tr("--")),
                   textOr(state.chargerTypeText, tr("类型未知")),
                   textOr(state.ratedPowerText, tr("功率未知")))
        : tr("扫码启动后可在此切换充电桩"));
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
    ui->scanChargingButton->setText(hasSession ? tr("扫码充电")
                                                  : tr("扫码开始充电"));
    applySelectedSession(state.orderId);
}

void ChargingSessionWindow::renderSessions(const ChargingSessionCollectionViewState &state)
{
    m_collection = state;
    m_updatingSelector = true;
    const QSignalBlocker blocker(ui->sessionSelectorButton);
    ui->sessionSelectorButton->clear();
    m_sessionChooser->clear();
    for (const ChargingSessionSummaryView &session : state.sessions) {
        const QString title = tr("%1 · %2号桩   %3")
                                  .arg(textOr(session.stationName, tr("未知站点")),
                                       textOr(session.chargerCode, tr("--")),
                                       textOr(session.currentPowerText, session.statusText));
        ui->sessionSelectorButton->addItem(title, session.orderId);
        const int row = ui->sessionSelectorButton->count() - 1;
        ui->sessionSelectorButton->setItemData(row, QSize(0, 42), Qt::SizeHintRole);
        auto *chooserItem = new QListWidgetItem(title, m_sessionChooser);
        chooserItem->setData(Qt::UserRole, session.orderId);
        chooserItem->setSizeHint(QSize(0, 64));
        auto *chooserCard = new QWidget(m_sessionChooser);
        chooserCard->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *cardLayout = new QVBoxLayout(chooserCard);
        cardLayout->setContentsMargins(12, 7, 12, 7);
        cardLayout->setSpacing(3);
        auto *stationLabel = new QLabel(textOr(session.stationName, tr("未知站点")), chooserCard);
        stationLabel->setStyleSheet(QStringLiteral(
            "background: transparent; color: #13223F; font-size: 14px; font-weight: 700;"));
        stationLabel->setWordWrap(false);
        auto *detailLabel = new QLabel(
            tr("%1号桩  ·  %2  ·  %3")
                .arg(textOr(session.chargerCode, tr("--")),
                     textOr(session.currentPowerText, tr("功率未知")),
                     textOr(session.statusText, tr("充电中"))),
            chooserCard);
        detailLabel->setStyleSheet(QStringLiteral(
            "background: transparent; color: #62718A; font-size: 12px;"));
        cardLayout->addWidget(stationLabel);
        cardLayout->addWidget(detailLabel);
        m_sessionChooser->setItemWidget(chooserItem, chooserCard);
    }
    if (state.sessions.isEmpty()) {
        ui->sessionSelectorButton->addItem(tr("暂无进行中的充电"));
        ui->sessionSelectorButton->setEnabled(false);
        m_sessionChooserPanel->hide();
    } else {
        ui->sessionSelectorButton->setEnabled(true);
        applySelectedSession(state.selectedOrderId);
    }
    ui->sessionSelectorButton->setToolTip(state.message);
    m_updatingSelector = false;
}

void ChargingSessionWindow::animateProgress(int progress)
{
    m_progressAnimation->stop();
    m_progressAnimation->setStartValue(ui->chargingProgressRing->displayedProgress());
    m_progressAnimation->setEndValue(qBound(0, progress, 100));
    m_progressAnimation->start();
}

void ChargingSessionWindow::applySelectedSession(const QString &orderId)
{
    if (orderId.isEmpty()) return;
    for (int index = 0; index < ui->sessionSelectorButton->count(); ++index) {
        if (ui->sessionSelectorButton->itemData(index).toString() == orderId) {
            ui->sessionSelectorButton->setCurrentIndex(index);
            for (int row = 0; row < m_sessionChooser->count(); ++row) {
                QListWidgetItem *item = m_sessionChooser->item(row);
                if (item->data(Qt::UserRole).toString() == orderId) {
                    m_sessionChooser->setCurrentItem(item);
                    break;
                }
            }
            break;
        }
    }
}
