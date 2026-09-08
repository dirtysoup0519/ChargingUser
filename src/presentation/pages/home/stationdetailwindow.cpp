#include "stationdetailwindow.h"
#include "dragscrollhelper.h"
#include "ui_stationdetailwindow.h"
#include "presentation/widgets/map/interactivemapwidget.h"

#include <algorithm>
#include <QFrame>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>

namespace {
class ClickableChargerRow final : public QFrame
{
public:
    explicit ClickableChargerRow(QWidget *parent = nullptr) : QFrame(parent) {}
    std::function<void()> activated;

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        QFrame::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton && rect().contains(event->pos())
            && activated)
            activated();
    }
};

void passMouseToRow(QWidget *widget)
{
    widget->setAttribute(Qt::WA_TransparentForMouseEvents);
}
}

StationDetailWindow::StationDetailWindow(QWidget *parent)
    : QWidget(parent), ui(new Ui::StationDetailWindow)
{
    ui->setupUi(this);
    DragScrollHelper::enableFor(this);
    m_map = new InteractiveMapWidget(this);
    m_map->setMapKey(qEnvironmentVariable("TENCENT_MAP_KEY"));
    m_map->setMinimumHeight(180);
    m_map->setMaximumHeight(180);
    ui->contentLayout->insertWidget(0, m_map);
    m_reservationBanner = new QFrame(ui->pageBackground);
    m_reservationBanner->setObjectName(QStringLiteral("reservationBanner"));
    auto *reservationLayout = new QHBoxLayout(m_reservationBanner);
    reservationLayout->setContentsMargins(14, 10, 14, 10);
    reservationLayout->setSpacing(8);
    m_reservationTitle = new QLabel(m_reservationBanner);
    m_reservationTitle->setObjectName(QStringLiteral("reservationBannerTitle"));
    m_reservationCountdown = new QLabel(m_reservationBanner);
    m_reservationCountdown->setObjectName(QStringLiteral("reservationCountdown"));
    m_reservationCountdown->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    reservationLayout->addWidget(m_reservationTitle, 1);
    reservationLayout->addWidget(m_reservationCountdown);
    m_cancelReservationButton = new QPushButton(tr("取消预约"), m_reservationBanner);
    m_cancelReservationButton->setObjectName(QStringLiteral("cancelReservationButton"));
    reservationLayout->addWidget(m_cancelReservationButton);
    m_cancellationStateLabel = new QLabel(m_reservationBanner);
    m_cancellationStateLabel->setObjectName(QStringLiteral("cancellationStateLabel"));
    m_cancellationStateLabel->setWordWrap(true);
    reservationLayout->addWidget(m_cancellationStateLabel);
    ui->contentLayout->insertWidget(1, m_reservationBanner);
    m_reservationBanner->hide();
    m_reservationRestrictionLabel = new QLabel(ui->pageBackground);
    m_reservationRestrictionLabel->setObjectName(
        QStringLiteral("reservationRestrictionLabel"));
    m_reservationRestrictionLabel->setAlignment(Qt::AlignCenter);
    m_reservationRestrictionLabel->setWordWrap(true);
    ui->contentLayout->insertWidget(2, m_reservationRestrictionLabel);
    m_reservationRestrictionLabel->hide();
    m_reservationTimer = new QTimer(this);
    m_reservationTimer->setInterval(1000);
    connect(m_reservationTimer, &QTimer::timeout,
            this, &StationDetailWindow::updateReservationCountdown);
    connect(m_cancelReservationButton, &QPushButton::clicked, this, [this] {
        if (!m_state.activeReservation
            || m_state.activeReservation->reservationId.isEmpty()) return;
        if (m_state.activeReservation->canRetryCancel)
            emit cancelReservationRetryRequested(m_state.activeReservation->reservationId);
        else if (m_state.activeReservation->canCancel)
            emit cancelReservationRequested(m_state.activeReservation->reservationId);
    });
    DragScrollHelper::prioritizeInteractiveWidget(m_map, ui->detailScroll);
    // 详情页没有独立的定位业务上下文；点击地图定位按钮时，
    // 将当前站点重新置于视野中心，避免按钮看起来无响应。
    connect(m_map, &InteractiveMapWidget::locateRequested, this, [this] {
        if (m_state.point && m_state.point->isValid())
            m_map->centerStation(m_state.stationId);
    });
    connect(ui->backButton, &QPushButton::clicked,
            this, &StationDetailWindow::backRequested);
    connect(ui->navigationButton, &QPushButton::clicked, this, [this] {
        if (!m_state.canNavigate || m_state.stationId.isEmpty())
            return;
        emit routePreviewRequested(TravelMode::Driving);
    });
    connect(ui->detailRetryButton, &QPushButton::clicked,
            this, &StationDetailWindow::stationRefreshRequested);
    connect(ui->chargeButton, &QPushButton::clicked, this, [this] {
        if (!m_state.stationId.isEmpty() && !m_state.selectedChargerId.isEmpty())
            emit chargeConfirmationRequested(m_state.stationId, m_state.selectedChargerId);
    });
    connect(ui->reservationButton, &QPushButton::clicked, this, [this] {
        if (m_state.activeReservation) {
            const bool ownReservationSelected =
                m_state.activeReservation->stationId == m_state.stationId
                && m_state.activeReservation->chargerId == m_state.selectedChargerId;
            if (ownReservationSelected) {
                if (m_state.activeReservation->canRetryCancel)
                    emit cancelReservationRetryRequested(
                        m_state.activeReservation->reservationId);
                else if (m_state.activeReservation->canCancel)
                    emit cancelReservationRequested(
                        m_state.activeReservation->reservationId);
                return;
            }
            const QString chargerCode = m_state.activeReservation->chargerId
                                            .section(QLatin1Char('-'), -1)
                                            .toUpper();
            if (QMessageBox::question(
                    this, tr("查看已有预约"),
                    tr("当前账号已有预约。是否前往预约所属站点并查看充电桩 %1？")
                        .arg(chargerCode),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::Yes) != QMessageBox::Yes)
                return;
            emit activeReservationRequested(m_state.activeReservation->reservationId,
                                            m_state.activeReservation->stationId,
                                            m_state.activeReservation->chargerId);
            return;
        }
        if (!m_state.stationId.isEmpty() && !m_state.selectedChargerId.isEmpty())
            emit reservationConfirmationRequested(m_state.stationId,
                                                  m_state.selectedChargerId);
    });
    ui->openBadge->hide();
    ui->feeNote->hide();
    ui->chargeButton->setEnabled(false);
    render(StationDetailViewState{});
}

StationDetailWindow::~StationDetailWindow() { delete ui; }

void StationDetailWindow::render(const StationDetailViewState &state)
{
    m_state = state;
    m_expiryRefreshEmitted = false;
    const bool hasActiveReservation = state.activeReservation.has_value();
    const bool reservationRestricted = !hasActiveReservation
                                       && !state.canCreateReservation
                                       && !state.reservationDisabledReason.isEmpty();
    m_reservationRestrictionLabel->setText(
        reservationRestricted
            ? tr("预约冷却中：%1").arg(state.reservationDisabledReason)
            : QString());
    m_reservationRestrictionLabel->setVisible(reservationRestricted);
    const bool hasReservationHere = hasActiveReservation
                                    && state.activeReservation->stationId == state.stationId;
    m_reservationBanner->setVisible(hasReservationHere);
    if (hasReservationHere) {
        m_reservationTitle->setText(tr("已预约充电桩 %1")
                                        .arg(state.activeReservation->chargerId.section(
                                            QLatin1Char('-'), -1).toUpper()));
        updateReservationCountdown();
        const bool cancelBusy = state.activeReservation->cancellationStatus
                                == ReservationCancellationStatus::Submitting
                                || state.activeReservation->cancellationStatus
                                       == ReservationCancellationStatus::ResultUnknown;
        m_cancelReservationButton->setEnabled(!cancelBusy
                                              && (state.activeReservation->canCancel
                                                  || state.activeReservation->canRetryCancel));
        m_cancelReservationButton->setText(
            cancelBusy ? tr("取消中…")
                       : state.activeReservation->canRetryCancel
                             ? tr("重试取消") : tr("取消预约"));
        m_cancellationStateLabel->setText(state.activeReservation->cancellationMessage);
        m_cancellationStateLabel->setVisible(
            !state.activeReservation->cancellationMessage.isEmpty());
        m_cancelReservationButton->setToolTip(
            state.activeReservation->canCancel
                ? QString() : state.activeReservation->cancelDisabledReason);
        m_reservationTimer->start();
    } else {
        m_reservationTimer->stop();
    }
    if (m_map) {
        QList<InteractiveMapWidget::Marker> markers;
        if (state.point && state.point->isValid())
            markers.append({state.stationId, QPointF(0.5, 0.5), true, *state.point});
        m_map->setMarkers(markers);
        m_map->setSelectedStation(state.stationId);
        m_map->renderMapStatus(state.status, state.message, state.canRetry);
    }
    const QString stationName = state.name.isEmpty() ? tr("充电站详情") : state.name;
    const QString availability = state.availabilityText.isEmpty()
                                     ? tr("可用情况待加载")
                                     : state.availabilityText;
    const QString price = state.priceText.isEmpty() ? tr("--") : state.priceText;
    ui->stationName->setText(stationName);

    ui->addressLabel->setText(state.address.isEmpty() ? tr("地址待加载") : state.address);
    ui->availabilityLabel->setText(availability);
    ui->priceLabel->setText(price);

    const int totalCount = state.chargers.size();
    const int availableCount = std::count_if(
        state.chargers.cbegin(), state.chargers.cend(),
        [](const ChargerListItemView &charger) { return charger.canCharge; });
    const int unavailableCount = totalCount - availableCount;
    const bool hasChargerData = totalCount > 0;
    ui->availableCountValue->setText(hasChargerData
                                         ? QString::number(availableCount)
                                         : tr("--"));
    ui->unavailableCountValue->setText(hasChargerData
                                           ? QString::number(unavailableCount)
                                           : tr("--"));
    ui->totalCountValue->setText(hasChargerData
                                     ? QString::number(totalCount)
                                     : tr("--"));

    const bool loading = state.status == MapLoadStatus::Loading;
    const bool ready = state.status == MapLoadStatus::Ready;
    QString message = state.message;
    if (message.isEmpty()) {
        if (loading)
            message = tr("正在加载充电站详情…");
        else if (state.status == MapLoadStatus::Empty)
            message = tr("暂无可用的充电站详情");
        else if (state.status == MapLoadStatus::Error)
            message = tr("充电站详情加载失败");
    }
    ui->detailStateLabel->setText(message);
    ui->detailStateLabel->setVisible(!message.isEmpty());
    ui->detailStateLabel->setProperty(
        "state", loading ? "loading" : state.status == MapLoadStatus::Error ? "error" : "neutral");
    ui->detailStateLabel->style()->unpolish(ui->detailStateLabel);
    ui->detailStateLabel->style()->polish(ui->detailStateLabel);
    ui->detailRetryButton->setVisible(state.canRetry && !loading);
    ui->detailRetryButton->setEnabled(state.canRetry && !loading);

    // 刷新期间和刷新失败后保留上一次成功内容，只通过状态文案标明新鲜度。
    rebuildChargers(state.chargers);
    if (!state.selectedChargerId.isEmpty()) {
        const QString selectedId = state.selectedChargerId;
        QTimer::singleShot(0, this, [this, selectedId] {
            const auto rows = ui->chargerListHost->findChildren<QFrame *>(
                QStringLiteral("chargerRow"), Qt::FindDirectChildrenOnly);
            for (QFrame *row : rows) {
                if (row->property("chargerId").toString() != selectedId) continue;
                ui->detailScroll->ensureWidgetVisible(row, 0, 72);
                break;
            }
        });
    }
    ui->chargerListHost->setVisible(!state.chargers.isEmpty());
    ui->sectionTitle->setVisible(ready || !state.chargers.isEmpty());

    ui->navigationButton->setEnabled(ready && state.canNavigate
                                     && !state.stationId.isEmpty());
    ui->navigationButton->setToolTip(
        ui->navigationButton->isEnabled() ? QString() : state.disabledReason);

    const bool ownReservationSelected = hasActiveReservation
                                        && state.activeReservation->stationId == state.stationId
                                        && state.activeReservation->chargerId
                                               == state.selectedChargerId;
    const bool canConfirm = ready
                            && (state.canContinueToConfirmation || ownReservationSelected)
                            && !state.stationId.isEmpty()
                            && !state.selectedChargerId.isEmpty();
    ui->chargeButton->setEnabled(canConfirm);
    ui->chargeButton->setText(canConfirm ? tr("扫码充电")
                                         : state.canCharge ? tr("请选择充电桩")
                                                           : tr("暂不可充电"));
    ui->chargeButton->setToolTip(canConfirm ? QString() : state.chargingDisabledReason);
    const bool cancellationBusy = ownReservationSelected
                                  && (state.activeReservation->cancellationStatus
                                          == ReservationCancellationStatus::Submitting
                                      || state.activeReservation->cancellationStatus
                                             == ReservationCancellationStatus::ResultUnknown);
    const bool canUseCancelAction = ownReservationSelected && !cancellationBusy
                                    && (state.activeReservation->canCancel
                                        || state.activeReservation->canRetryCancel);
    ui->reservationButton->setVisible(canConfirm || hasActiveReservation);
    if (ownReservationSelected) {
        ui->reservationButton->setText(
            cancellationBusy ? tr("取消中…")
                             : state.activeReservation->canRetryCancel
                                   ? tr("重试取消") : tr("取消预约"));
        ui->reservationButton->setEnabled(canUseCancelAction);
        ui->reservationButton->setToolTip(
            canUseCancelAction ? QString() : state.activeReservation->cancelDisabledReason);
    } else if (hasActiveReservation) {
        ui->reservationButton->setText(tr("查看已有预约"));
        ui->reservationButton->setEnabled(true);
        ui->reservationButton->setToolTip(tr("查看预约所属站点和充电桩"));
    } else {
        ui->reservationButton->setText(tr("前往预约"));
        ui->reservationButton->setEnabled(canConfirm && state.canCreateReservation);
        ui->reservationButton->setToolTip(
            state.canCreateReservation ? QString() : state.reservationDisabledReason);
    }
}

void StationDetailWindow::rebuildChargers(
    const QVector<ChargerListItemView> &chargers)
{
    while (QLayoutItem *item = ui->chargerListLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            // deleteLater 在事件循环处理前，旧行仍是 chargerListHost 的子对象，
            // 会被 findChildren 统计、也会短暂叠加显示。先脱离父对象再延迟销毁：
            // 既避免在子控件自己的信号槽调用栈里同步 delete 的崩溃风险，
            // 又保证重建立刻生效（修复 detailRefreshKeepsLastSuccessfulChargers）。
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        delete item;
    }

    for (const ChargerListItemView &charger : chargers) {
        const bool isOwnReservation = m_state.activeReservation.has_value()
                                      && m_state.activeReservation->stationId == m_state.stationId
                                      && m_state.activeReservation->chargerId == charger.chargerId;
        auto *row = new ClickableChargerRow(ui->chargerListHost);
        row->setObjectName(QStringLiteral("chargerRow"));
        row->setProperty("chargerId", charger.chargerId);
        row->setProperty("available", charger.canCharge);
        row->setProperty("reserved", isOwnReservation);
        row->setProperty("selected", charger.chargerId == m_state.selectedChargerId);
        row->setMinimumHeight(82);
        if (charger.canCharge || isOwnReservation) {
            row->setCursor(Qt::PointingHandCursor);
            row->activated = [this, charger] { selectCharger(charger); };
        }

        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(12, 9, 10, 9);
        layout->setSpacing(11);

        QString displayId = charger.chargerId;
        if (displayId.size() > 8)
            displayId = displayId.section(QLatin1Char('-'), -1).toUpper();
        auto *idLabel = new QLabel(displayId, row);
        idLabel->setObjectName(QStringLiteral("chargerIdLabel"));
        idLabel->setToolTip(charger.chargerId);
        idLabel->setFixedSize(58, 56);
        idLabel->setAlignment(Qt::AlignCenter);
        passMouseToRow(idLabel);
        layout->addWidget(idLabel);

        auto *textBlock = new QWidget(row);
        textBlock->setObjectName(QStringLiteral("chargerTextBlock"));
        passMouseToRow(textBlock);
        auto *textLayout = new QVBoxLayout(textBlock);
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(3);
        auto *title = new QLabel(charger.title, textBlock);
        title->setObjectName(QStringLiteral("chargerName"));
        auto *spec = new QLabel(charger.powerText, textBlock);
        spec->setObjectName(QStringLiteral("chargerSpec"));
        textLayout->addWidget(title);
        textLayout->addWidget(spec);
        layout->addWidget(textBlock, 1);

        auto *status = new QLabel(charger.statusText, row);
        status->setObjectName(isOwnReservation
                                  ? QStringLiteral("reservationBadge")
                                  : charger.canCharge ? QStringLiteral("availableBadge")
                                                      : QStringLiteral("busyBadge"));
        status->setToolTip(charger.disabledReason);
        status->setFixedSize(46, 24);
        status->setAlignment(Qt::AlignCenter);
        passMouseToRow(status);
        layout->addWidget(status);

        auto *selectButton = new QPushButton(row);
        selectButton->setObjectName(QStringLiteral("chargerSelectButton"));
        selectButton->setFixedSize(52, 32);
        selectButton->setEnabled(charger.canCharge || isOwnReservation);
        selectButton->setText(charger.chargerId == m_state.selectedChargerId
                                  ? tr("已选")
                                  : (charger.canCharge || isOwnReservation)
                                        ? tr("选择") : tr("不可用"));
        selectButton->setToolTip(isOwnReservation ? tr("选择本人已预约的充电桩")
                                                   : charger.canCharge
                                                         ? tr("选择此充电桩")
                                                         : charger.disabledReason);
        connect(selectButton, &QPushButton::clicked, this, [this, charger] {
            selectCharger(charger);
        });
        layout->addWidget(selectButton);
        ui->chargerListLayout->addWidget(row);
    }
}

void StationDetailWindow::selectCharger(const ChargerListItemView &charger)
{
    const bool isOwnReservation = m_state.activeReservation.has_value()
                                  && m_state.activeReservation->stationId == m_state.stationId
                                  && m_state.activeReservation->chargerId == charger.chargerId;
    if ((!charger.canCharge && !isOwnReservation) || charger.chargerId.isEmpty())
        return;
    if (isOwnReservation) {
        m_state.selectedChargerId = charger.chargerId;
        m_state.canContinueToConfirmation = true;
        render(m_state);
        return;
    }
    emit chargerSelected(charger.chargerId);
}

void StationDetailWindow::updateReservationCountdown()
{
    if (!m_state.activeReservation || !m_reservationBanner->isVisible()) {
        m_reservationTimer->stop();
        return;
    }
    const ActiveReservationView &reservation = *m_state.activeReservation;
    qint64 seconds = QDateTime::currentDateTimeUtc().secsTo(reservation.expiresAtUtc);
    seconds = qMax<qint64>(0, seconds);
    const qint64 minutes = seconds / 60;
    const qint64 remainder = seconds % 60;
    m_reservationCountdown->setText(
        tr("剩余 %1:%2").arg(minutes, 2, 10, QLatin1Char('0'))
                           .arg(remainder, 2, 10, QLatin1Char('0')));
    if (seconds == 0 && !m_expiryRefreshEmitted) {
        m_expiryRefreshEmitted = true;
        m_reservationTimer->stop();
        emit reservationExpiredRefreshRequested();
    }
}
