#include "navigationwindow.h"
#include "dragscrollhelper.h"
#include "ui_navigationwindow.h"
#include "routepreviewwidget.h"

#include <QLabel>
#include <QLineEdit>
#include <QFrame>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStyle>
#include <QVBoxLayout>

NavigationWindow::NavigationWindow(QWidget *parent)
    : QWidget(parent), ui(new Ui::NavigationWindow)
{
    ui->setupUi(this);
    DragScrollHelper::enableFor(this);

    // The origin editor is an overlay: opening it must not squeeze the route
    // map and summary into an unusably small area.
    ui->contentLayout->removeWidget(ui->originEditorPanel);
    ui->originEditorPanel->setParent(ui->navigationBackground);
    ui->originEditorPanel->setGeometry(12, 48, 366, 250);
    ui->originEditorPanel->hide();
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
    connect(ui->modifyOriginButton, &QPushButton::clicked, this, [this] {
        setOriginEditorOpen(!m_originEditorOpen);
    });
    connect(ui->originEditorBackButton, &QPushButton::clicked, this, [this] {
        setOriginEditorOpen(false);
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
                                     ? tr("收起详情")
                                     : tr("路线详情"));
    });
    render(NavigationViewState{});
}

NavigationWindow::~NavigationWindow() { delete ui; }

void NavigationWindow::render(const NavigationViewState &state)
{
    m_state = state;
    ui->originLabel->setText(state.originText.isEmpty()
                                 ? tr("起点待确认")
                                 : state.originText);
    ui->destinationLabel->setText(state.destinationText.isEmpty()
                                      ? tr("目的地待加载")
                                      : state.destinationText);

    {
        const QSignalBlocker driveBlocker(ui->driveButton);
        const QSignalBlocker walkBlocker(ui->walkButton);
        ui->driveButton->setChecked(state.mode == TravelMode::Driving);
        ui->walkButton->setChecked(state.mode == TravelMode::Walking);
    }
    ui->driveButton->setEnabled(state.canChangeMode);
    ui->walkButton->setEnabled(state.canChangeMode);

    rebuildOriginCandidates(state.originCandidates);
    if (!state.originCandidates.isEmpty())
        setOriginEditorOpen(true);
    else if (m_originSubmissionPending && state.origin.has_value()) {
        m_originSubmissionPending = false;
        setOriginEditorOpen(false);
    }

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

    if (m_originEditorOpen && state.originCandidates.isEmpty()) {
        QString originHint = tr("输入地点名称，搜索结果会显示在这里");
        if (m_originSubmissionPending && loading)
            originHint = state.message.isEmpty() ? tr("正在搜索相关地点…") : state.message;
        else if (m_originSubmissionPending
                 && (state.routeStatus == MapLoadStatus::Empty
                     || state.routeStatus == MapLoadStatus::Error))
            originHint = state.message;
        ui->originCandidatesEmptyLabel->setText(originHint);
    }
    ui->manualOriginButton->setEnabled(!(m_originSubmissionPending && loading));

    if (ready) {
        const RouteViewData &route = *state.route;
        ui->routeInfo->setText(route.distanceText);
        ui->routeTime->setText(route.durationText);
        ui->routeMap->setRoutePolyline(route.polyline);
        ui->routeMap->setOrigin(state.origin);
        ui->routeMap->setDestination(state.destination);
        rebuildRouteSteps(route.steps);
        ui->startButton->setVisible(!route.steps.isEmpty());
    } else {
        ui->routeInfo->setText(QStringLiteral("--"));
        ui->routeTime->setText(QStringLiteral("--"));
        ui->routeMap->clearRoute();
        ui->routeMap->setOrigin(state.origin);
        ui->routeMap->setDestination(state.destination);
        rebuildRouteSteps({});
        ui->routeStepsScroll->hide();
        ui->startButton->hide();
    }
    ui->startButton->setText(tr("路线详情"));
}

void NavigationWindow::setOriginEditorOpen(bool open)
{
    m_originEditorOpen = open;
    ui->originEditorPanel->setVisible(open);
    ui->modifyOriginButton->setText(open ? tr("收起") : tr("修改"));
    if (!open) {
        m_originSubmissionPending = false;
        return;
    }
    ui->originEditorPanel->raise();
    ui->manualOriginEdit->setFocus(Qt::OtherFocusReason);
    ui->manualOriginEdit->selectAll();
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
    m_originSubmissionPending = true;
    ui->manualOriginButton->setEnabled(false);
    ui->originCandidatesEmptyLabel->setText(tr("正在搜索相关地点…"));
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
            m_originSubmissionPending = false;
            setOriginEditorOpen(false);
            emit originCandidateSelected(id);
        });
        ui->originCandidatesLayout->addWidget(button);
    }
    const bool hasCandidates = !candidates.isEmpty();
    ui->originCandidatesScroll->setVisible(hasCandidates);
    ui->originCandidatesEmptyLabel->setVisible(!hasCandidates);
}

void NavigationWindow::rebuildRouteSteps(const QVector<RouteStepView> &steps)
{
    while (QLayoutItem *item = ui->routeStepsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    int index = 0;
    for (const RouteStepView &step : steps) {
        auto *row = new QFrame(ui->routeStepsHost);
        row->setObjectName(QStringLiteral("routeStepRow"));
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(8, 7, 8, 7);
        layout->setSpacing(9);

        auto *number = new QLabel(QString::number(index + 1), row);
        number->setObjectName(QStringLiteral("routeStepNumber"));
        number->setFixedSize(26, 26);
        number->setAlignment(Qt::AlignCenter);
        layout->addWidget(number);

        auto *instruction = new QLabel(step.instruction, row);
        instruction->setObjectName(QStringLiteral("routeStepInstruction"));
        instruction->setWordWrap(true);
        layout->addWidget(instruction, 1);

        auto *distance = new QLabel(step.distanceText, row);
        distance->setObjectName(QStringLiteral("routeStepDistance"));
        distance->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        distance->setMinimumWidth(52);
        layout->addWidget(distance);

        row->setProperty(
            "endpoint",
            index == 0
                ? QStringLiteral("start")
                : index == steps.size() - 1
                      ? QStringLiteral("end")
                      : QStringLiteral("middle"));
        ui->routeStepsLayout->addWidget(row);
        ++index;
    }
    ui->routeStepsLayout->addStretch();
}
