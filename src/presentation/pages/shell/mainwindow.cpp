#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "interactivemapwidget.h"
#include <algorithm>
#include <QList>
#include <QLineEdit>
#include <QPair>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QWidget(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowFlag(Qt::FramelessWindowHint);

    const QList<QPair<QPushButton *, QString>> stationButtons = {
        {ui->stationButton1, QStringLiteral("station-sz-civic-center")},
        {ui->stationButton2, QStringLiteral("station-futian-cbd")},
        {ui->stationButton3, QStringLiteral("station-nanshan-tech-park")}
    };
    for (const auto &entry : stationButtons) {
        m_stationButtons.insert(entry.second, entry.first);
        entry.first->setProperty("stationId", entry.second);
        connect(entry.first, &QPushButton::clicked, this, [this, entry] {
            selectStation(entry.second, true);
        });
        addDetailsButton(entry.first, entry.second);
    }

    ui->mapView->setMarkers({
        {stationButtons.at(0).second, QPointF(0.42, 0.44), true},
        {stationButtons.at(1).second, QPointF(0.18, 0.62), true},
        {stationButtons.at(2).second, QPointF(0.76, 0.28), true}
    });
    connect(ui->mapView, &InteractiveMapWidget::markerSelected,
            this, [this](const QString &stationId) { selectStation(stationId, true); });
    connect(ui->mapView, &InteractiveMapWidget::locateRequested,
            this, &MainWindow::locateRequested);
    connect(ui->mapView, &InteractiveMapWidget::searchAreaRequested,
            this, [this] {
        if (m_viewportBounds && m_viewportBounds->isValid())
            emit searchAreaRequested(*m_viewportBounds);
    });
    connect(ui->btnStationSearch, &QPushButton::clicked, this, [this] {
        const QString keyword = ui->searchBox->text().trimmed();
        if (keyword.isEmpty())
            emit stationSearchCleared();
        else
            emit stationSearchRequested(keyword);
    });
    connect(ui->searchBox, &QLineEdit::returnPressed, this, [this] {
        if (!ui->btnStationSearch->isEnabled())
            return;
        const QString keyword = ui->searchBox->text().trimmed();
        if (keyword.isEmpty())
            emit stationSearchCleared();
        else
            emit stationSearchRequested(keyword);
    });
    connect(ui->searchRetryButton, &QPushButton::clicked,
            this, &MainWindow::stationSearchRetryRequested);

    connect(ui->homeNav, &QToolButton::clicked, this, [this] {
        emit primaryPageRequested(PrimaryPage::Home);
    });
    connect(ui->chargeNav, &QToolButton::clicked, this, [this] {
        emit primaryPageRequested(PrimaryPage::Charging);
    });
    connect(ui->profileNav, &QToolButton::clicked, this, [this] {
        emit primaryPageRequested(PrimaryPage::Profile);
    });
    connect(ui->btnEditProfile, &QToolButton::clicked,
            this, &MainWindow::profileEditRequested);
    connect(ui->btnRecharge, &QPushButton::clicked,
            this, &MainWindow::rechargePageRequested);
    connect(ui->btnLogout, &QPushButton::clicked,
            this, &MainWindow::logoutRequested);
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::renderHome(const HomeMapViewState &state)
{
    if (!state.queryInput.isNull() && !ui->searchBox->hasFocus())
        ui->searchBox->setText(state.queryInput);

    const bool searching = state.searchStatus == MapLoadStatus::Loading;
    ui->btnStationSearch->setEnabled(state.canSearch && !searching);
    ui->btnStationSearch->setText(searching ? tr("搜索中…") : tr("搜索"));
    ui->searchErrorLabel->setText(state.searchMessage);
    ui->searchErrorLabel->setVisible(!state.searchMessage.isEmpty());
    ui->searchRetryButton->setVisible(!state.searchMessage.isEmpty()
                                      && state.canRetrySearch);
    ui->mapView->setLocateEnabled(state.locationStatus != MapLoadStatus::Loading);
    m_viewportBounds = state.camera.bounds;

    rebuildStationRows(state.stations);

    QList<InteractiveMapWidget::Marker> markers;
    const auto firstValidMarker = std::find_if(
        state.markers.cbegin(), state.markers.cend(),
        [](const MapMarkerView &marker) { return marker.point.isValid(); });
    if (firstValidMarker != state.markers.cend()) {
        double minLatitude = firstValidMarker->point.latitude;
        double maxLatitude = minLatitude;
        double minLongitude = firstValidMarker->point.longitude;
        double maxLongitude = minLongitude;
        for (const MapMarkerView &marker : state.markers) {
            if (!marker.point.isValid())
                continue;
            minLatitude = std::min(minLatitude, marker.point.latitude);
            maxLatitude = std::max(maxLatitude, marker.point.latitude);
            minLongitude = std::min(minLongitude, marker.point.longitude);
            maxLongitude = std::max(maxLongitude, marker.point.longitude);
        }
        const double latitudeSpan = std::max(0.000001, maxLatitude - minLatitude);
        const double longitudeSpan = std::max(0.000001, maxLongitude - minLongitude);
        for (const MapMarkerView &marker : state.markers) {
            if (!marker.point.isValid())
                continue;
            markers.append({marker.stationId,
                            QPointF((marker.point.longitude - minLongitude) / longitudeSpan,
                                    1.0 - (marker.point.latitude - minLatitude) / latitudeSpan),
                            true});
        }
    }
    ui->mapView->setMarkers(markers);
    selectStation(state.selectedStationId, false);

    if (state.cameraCommand.revision != m_cameraRevision) {
        m_cameraRevision = state.cameraCommand.revision;
        if (state.cameraCommand.type == MapCameraCommandType::CenterStation)
            ui->mapView->centerStation(state.cameraCommand.stationId);
        else if (state.cameraCommand.type == MapCameraCommandType::FitStations) {
            QStringList stationIds;
            for (const QString &stationId : state.cameraCommand.stationIds)
                stationIds.append(stationId);
            ui->mapView->fitStations(stationIds);
        }
    }
}

void MainWindow::registerSecondaryPage(QWidget *page)
{
    if (!page || ui->pageStack->indexOf(page) >= 0)
        return;
    ui->pageStack->addWidget(page);
}

void MainWindow::renderPrimaryPage(PrimaryPage page)
{
    QWidget *target = ui->homePage;
    if (page == PrimaryPage::Charging)
        target = ui->chargingPage;
    else if (page == PrimaryPage::Profile)
        target = ui->profilePage;

    ui->pageStack->setCurrentWidget(target);
    ui->bottomBar->show();
    ui->homeNav->setChecked(page == PrimaryPage::Home);
    ui->chargeNav->setChecked(page == PrimaryPage::Charging);
    ui->profileNav->setChecked(page == PrimaryPage::Profile);
}

void MainWindow::renderProfile(const ProfileViewState &state)
{
    const QString nickname = state.nickname.isEmpty() ? tr("未设置昵称") : state.nickname;
    const QString phone = state.maskedPhone.isEmpty() ? tr("手机号待加载") : state.maskedPhone;
    ui->profileSummaryLabel->setText(nickname);
    ui->profilePhoneLabel->setText(phone);
    ui->walletSummaryLabel->setText(
        state.balanceText.isEmpty() ? QStringLiteral("--") : state.balanceText);

    const bool restricted = state.accountState != AccountDisplayState::Normal;
    QString accountMessage = state.accountMessage;
    if (accountMessage.isEmpty() && state.accountState == AccountDisplayState::Frozen)
        accountMessage = tr("当前账号已受限，部分操作暂不可用。");
    else if (accountMessage.isEmpty() && state.accountState == AccountDisplayState::Unknown)
        accountMessage = tr("账号状态尚未确认，请等待刷新后再操作。");
    ui->accountStatusLabel->setText(accountMessage);
    ui->accountStatusLabel->setVisible(restricted);
    // Frozen 用户按 M1 合同仍可充值和支付；只有状态未确认时暂时禁用充值。
    ui->btnRecharge->setEnabled(state.accountState != AccountDisplayState::Unknown);
}

void MainWindow::renderSecondaryPage(QWidget *page)
{
    if (!page)
        return;
    registerSecondaryPage(page);
    ui->pageStack->setCurrentWidget(page);
    ui->bottomBar->hide();
}

void MainWindow::rebuildStationRows(const QVector<StationListItemView> &stations)
{
    const auto oldButtons = m_stationButtons;
    m_stationButtons.clear();
    for (QPushButton *button : oldButtons) {
        ui->stationLayout->removeWidget(button);
        button->deleteLater();
    }

    int row = 0;
    for (const StationListItemView &station : stations) {
        if (station.stationId.isEmpty() || m_stationButtons.contains(station.stationId))
            continue;
        QPushButton *button = createStationButton(station);
        m_stationButtons.insert(station.stationId, button);
        ui->stationLayout->insertWidget(row++, button);
    }
    ui->hintLabel->setText(stations.isEmpty()
                               ? tr("暂无结果")
                               : tr("共 %1 个").arg(m_stationButtons.size()));
}

QPushButton *MainWindow::createStationButton(const StationListItemView &station)
{
    auto *button = new QPushButton(ui->stationList);
    button->setObjectName(QStringLiteral("stationRow"));
    button->setProperty("stationId", station.stationId);
    button->setProperty("selected", false);
    button->setMinimumHeight(72);
    button->setText(QStringLiteral("⚡  %1                       %2\n     %3                  %4   %5")
                        .arg(station.name, station.distanceText, station.address,
                             station.priceText, station.availabilityText));
    connect(button, &QPushButton::clicked, this,
            [this, id = station.stationId] { selectStation(id, true); });
    addDetailsButton(button, station.stationId);
    return button;
}

void MainWindow::addDetailsButton(QPushButton *stationButton,
                                  const QString &stationId)
{
    auto *layout = new QHBoxLayout(stationButton);
    layout->setContentsMargins(0, 0, 10, 0);
    layout->addStretch();

    auto *detailsButton = new QPushButton(tr("查看详情"), stationButton);
    detailsButton->setObjectName(QStringLiteral("stationDetailsButton"));
    detailsButton->setProperty("stationId", stationId);
    detailsButton->setFixedSize(72, 30);
    layout->addWidget(detailsButton, 0, Qt::AlignVCenter);
    connect(detailsButton, &QPushButton::clicked, this,
            [this, stationId] { emit stationDetailsRequested(stationId); });
}

void MainWindow::selectStation(const QString &stationId, bool emitIntent)
{
    for (auto it = m_stationButtons.cbegin(); it != m_stationButtons.cend(); ++it) {
        const bool selected = !stationId.isEmpty() && it.key() == stationId;
        it.value()->setProperty("selected", selected);
        it.value()->style()->unpolish(it.value());
        it.value()->style()->polish(it.value());
    }
    ui->mapView->setSelectedStation(stationId);
    if (!stationId.isEmpty() && m_stationButtons.contains(stationId)) {
        QPushButton *button = m_stationButtons.value(stationId);
        ui->stationLayout->removeWidget(button);
        ui->stationLayout->insertWidget(0, button);
        ui->stationScroll->ensureWidgetVisible(button, 0, 0);
        ui->mapView->centerStation(stationId);
    }
    if (emitIntent)
        emit stationSelected(stationId);
}
