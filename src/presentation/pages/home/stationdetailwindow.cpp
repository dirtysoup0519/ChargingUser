#include "stationdetailwindow.h"
#include "ui_stationdetailwindow.h"
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QVBoxLayout>

StationDetailWindow::StationDetailWindow(QWidget *parent)
    : QWidget(parent), ui(new Ui::StationDetailWindow)
{
    ui->setupUi(this);
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
    ui->stationName->setText(state.name.isEmpty() ? tr("充电站详情") : state.name);
    ui->addressLabel->setText(state.address.isEmpty() ? tr("地址待加载") : state.address);
    ui->availabilityLabel->setText(state.availabilityText.isEmpty()
                                       ? tr("可用情况待加载")
                                       : state.availabilityText);
    ui->priceLabel->setText(state.priceText.isEmpty()
                                ? tr("当前电价\n--")
                                : tr("当前电价\n%1").arg(state.priceText));

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
        row->setMinimumHeight(68);

        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(12, 8, 12, 8);
        layout->setSpacing(10);

        auto *idLabel = new QLabel(charger.chargerId, row);
        idLabel->setObjectName(QStringLiteral("chargerIdLabel"));
        idLabel->setMinimumWidth(62);
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
        ui->chargerListLayout->addWidget(row);
    }
}
