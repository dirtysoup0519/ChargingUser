#include "navigationwindow.h"
#include "ui_navigationwindow.h"
#include "routepreviewwidget.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QVBoxLayout>

NavigationWindow::NavigationWindow(QWidget *parent)
    : QWidget(parent), ui(new Ui::NavigationWindow)
{
    ui->setupUi(this);
    setWindowFlag(Qt::FramelessWindowHint);
    connect(ui->backButton, &QPushButton::clicked,
            this, &NavigationWindow::backRequested);
    connect(ui->driveButton, &QPushButton::clicked, this, [this] {
        if (m_state.canChangeMode && m_state.mode != TravelMode::Driving)
            emit routeModeRequested(TravelMode::Driving);
    });
    connect(ui->walkButton, &QPushButton::clicked, this, [this] {
        if (m_state.canChangeMode && m_state.mode != TravelMode::Walking)
            emit routeModeRequested(TravelMode::Walking);
    });
    connect(ui->manualOriginButton, &QPushButton::clicked,
            this, &NavigationWindow::submitManualOrigin);
    connect(ui->manualOriginEdit, &QLineEdit::returnPressed,
            this, &NavigationWindow::submitManualOrigin);
    connect(ui->routeRetryButton, &QPushButton::clicked,
            this, &NavigationWindow::routeRetryRequested);
    connect(ui->startButton, &QPushButton::clicked, this, [this] {
        ui->routeStepsScroll->setVisible(!ui->routeStepsScroll->isVisible());
        ui->startButton->setText(ui->routeStepsScroll->isVisible()
                                     ? tr("收起路线步骤")
                                     : tr("查看路线步骤"));
    });
    render(NavigationViewState{});
}

NavigationWindow::~NavigationWindow() { delete ui; }

void NavigationWindow::render(const NavigationViewState &state)
{
    m_state = state;
    ui->originLabel->setText(state.originText.isEmpty()
                                 ? tr("● 起点待确认")
                                 : tr("● %1").arg(state.originText));
    ui->destinationLabel->setText(state.destinationText.isEmpty()
                                      ? tr("● 目的地待加载")
                                      : tr("● %1").arg(state.destinationText));

    {
        const QSignalBlocker driveBlocker(ui->driveButton);
        const QSignalBlocker walkBlocker(ui->walkButton);
        ui->driveButton->setChecked(state.mode == TravelMode::Driving);
        ui->walkButton->setChecked(state.mode == TravelMode::Walking);
    }
    ui->driveButton->setEnabled(state.canChangeMode);
    ui->walkButton->setEnabled(state.canChangeMode);

    rebuildOriginCandidates(state.originCandidates);

    const bool loading = state.routeStatus == MapLoadStatus::Loading;
    const bool ready = state.routeStatus == MapLoadStatus::Ready
                       && state.route.has_value();
    QString message = state.message;
    if (message.isEmpty()) {
        if (loading)
            message = tr("正在规划路线…");
        else if (state.routeStatus == MapLoadStatus::Empty)
            message = tr("暂无可用路线，请更换起点或出行方式");
        else if (state.routeStatus == MapLoadStatus::Error)
            message = tr("路线规划失败");
    }
    ui->routeStateLabel->setText(message);
    ui->routeStateLabel->setVisible(!message.isEmpty());
    ui->routeStateLabel->setProperty(
        "state", loading ? "loading" : state.routeStatus == MapLoadStatus::Error ? "error" : "neutral");
    ui->routeStateLabel->style()->unpolish(ui->routeStateLabel);
    ui->routeStateLabel->style()->polish(ui->routeStateLabel);
    ui->routeRetryButton->setVisible(state.canRetry && !loading);
    ui->routeRetryButton->setEnabled(state.canRetry && !loading);

    if (ready) {
        const RouteViewData &route = *state.route;
        ui->routeInfo->setText(tr("推荐路线\n%1").arg(route.distanceText));
        ui->routeTime->setText(route.durationText);
        ui->routeMap->setRoutePolyline(route.polyline);
        ui->routeMap->setOrigin(state.origin);
        ui->routeMap->setDestination(state.destination);
        rebuildRouteSteps(route.steps);
        ui->startButton->setVisible(!route.steps.isEmpty());
    } else {
        ui->routeInfo->setText(tr("路线信息待加载"));
        ui->routeTime->setText(QStringLiteral("--"));
        ui->routeMap->clearRoute();
        ui->routeMap->setOrigin(state.origin);
        ui->routeMap->setDestination(state.destination);
        rebuildRouteSteps({});
        ui->routeStepsScroll->hide();
        ui->startButton->hide();
    }
    ui->startButton->setText(tr("查看路线步骤"));
}

void NavigationWindow::submitManualOrigin()
{
    const QString address = ui->manualOriginEdit->text().trimmed();
    if (address.isEmpty()) {
        ui->routeStateLabel->setText(tr("请输入起点地址"));
        ui->routeStateLabel->setProperty("state", "error");
        ui->routeStateLabel->show();
        ui->routeStateLabel->style()->unpolish(ui->routeStateLabel);
        ui->routeStateLabel->style()->polish(ui->routeStateLabel);
        return;
    }
    emit manualOriginRequested(address);
}

void NavigationWindow::rebuildOriginCandidates(
    const QVector<GeocodeCandidateView> &candidates)
{
    while (QLayoutItem *item = ui->originCandidatesLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    for (const GeocodeCandidateView &candidate : candidates) {
        auto *button = new QPushButton(
            tr("%1\n%2").arg(candidate.name, candidate.fullAddress),
            ui->originCandidatesHost);
        button->setObjectName(QStringLiteral("originCandidateButton"));
        button->setProperty("candidateId", candidate.candidateId);
        connect(button, &QPushButton::clicked, this,
                [this, id = candidate.candidateId] {
            emit originCandidateSelected(id);
        });
        ui->originCandidatesLayout->addWidget(button);
    }
    ui->originCandidatesHost->setVisible(!candidates.isEmpty());
}

void NavigationWindow::rebuildRouteSteps(const QVector<RouteStepView> &steps)
{
    while (QLayoutItem *item = ui->routeStepsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    int index = 1;
    for (const RouteStepView &step : steps) {
        auto *label = new QLabel(
            tr("%1. %2  %3").arg(index++).arg(step.instruction, step.distanceText),
            ui->routeStepsHost);
        label->setObjectName(QStringLiteral("routeStepLabel"));
        label->setWordWrap(true);
        ui->routeStepsLayout->addWidget(label);
    }
}
