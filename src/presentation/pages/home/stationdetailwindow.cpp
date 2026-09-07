#include "stationdetailwindow.h"
#include "dragscrollhelper.h"
#include "ui_stationdetailwindow.h"
#include "presentation/widgets/map/interactivemapwidget.h"

#include <algorithm>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QVBoxLayout>

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
    ui->openBadge->hide();
    ui->feeNote->hide();
    ui->chargeButton->setEnabled(false);
    render(StationDetailViewState{});
}

StationDetailWindow::~StationDetailWindow() { delete ui; }

void StationDetailWindow::render(const StationDetailViewState &state)
{
    m_state = state;
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
    ui->chargerListHost->setVisible(!state.chargers.isEmpty());
    ui->sectionTitle->setVisible(ready || !state.chargers.isEmpty());

    ui->navigationButton->setEnabled(ready && state.canNavigate
                                     && !state.stationId.isEmpty());
    ui->navigationButton->setToolTip(
        ui->navigationButton->isEnabled() ? QString() : state.disabledReason);

    // 冻结地图合同尚无 chargerId 选择意图，不能安全进入充电确认。
    ui->chargeButton->setEnabled(false);
    ui->chargeButton->setText(state.canCharge
                                  ? tr("请选择充电桩")
                                  : tr("暂不可充电"));
    ui->chargeButton->setToolTip(state.disabledReason);
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
        auto *row = new QFrame(ui->chargerListHost);
        row->setObjectName(QStringLiteral("chargerRow"));
        row->setProperty("chargerId", charger.chargerId);
        row->setProperty("available", charger.canCharge);
        row->setMinimumHeight(82);

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
        layout->addWidget(idLabel);

        auto *textBlock = new QWidget(row);
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
        status->setObjectName(charger.canCharge
                                  ? QStringLiteral("availableBadge")
                                  : QStringLiteral("busyBadge"));
        status->setToolTip(charger.disabledReason);
        layout->addWidget(status);

        auto *actionIcon = new QLabel(row);
        actionIcon->setObjectName(QStringLiteral("chargerActionIcon"));
        actionIcon->setFixedSize(32, 32);
        const QString iconPath = charger.canCharge
                                     ? QStringLiteral(":/icons/station_charge.png")
                                     : QStringLiteral(":/icons/station_charge_active.png");
        actionIcon->setPixmap(QPixmap(iconPath).scaled(
            28, 28, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        actionIcon->setAlignment(Qt::AlignCenter);
        actionIcon->setToolTip(charger.canCharge
                                   ? tr("当前充电桩可用")
                                   : charger.disabledReason);
        layout->addWidget(actionIcon);
        ui->chargerListLayout->addWidget(row);
    }
}
