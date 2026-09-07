#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "interactivemapwidget.h"
#include <algorithm>
#include <limits>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QWidget(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    connect(ui->mapView, &InteractiveMapWidget::markerSelected,
            this, [this](const QString &stationId) { selectStation(stationId, true); });
    connect(ui->mapView, &InteractiveMapWidget::locateRequested,
            this, &MainWindow::locateRequested);
    connect(ui->mapView, &InteractiveMapWidget::mapReloadRequested,
            this, &MainWindow::mapReloadRequested);
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
    connect(ui->stationRetryButton, &QPushButton::clicked,
            this, &MainWindow::stationSearchRetryRequested);
    connect(ui->stationSortCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        m_stationSortMode = index == 1
                                ? StationSortMode::Availability
                                : StationSortMode::Distance;
        applyStationOrder(m_selectedStationId);
    });

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
    const bool keywordSearch = !state.submittedQuery.trimmed().isEmpty();
    const bool searchFailed = keywordSearch
                              && state.searchStatus == MapLoadStatus::Error
                              && !state.searchMessage.isEmpty();
    ui->searchErrorLabel->setText(state.searchMessage);
    ui->searchErrorLabel->setVisible(searchFailed);
    ui->searchRetryButton->setVisible(searchFailed && state.canRetrySearch);
    ui->mapView->setLocateEnabled(state.locationStatus != MapLoadStatus::Loading);
    ui->mapView->renderMapStatus(state.mapStatus, state.mapMessage,
                                 state.canRetryMap);
    ui->mapView->renderLocationStatus(state.locationStatus,
                                      state.locationMessage,
                                      state.canRetryLocation);
    m_viewportBounds = state.camera.bounds;

    QString stationStateMessage;
    QString stationStateKind;
    bool showStationState = false;
    bool showStationRetry = false;
    if (state.stationsStatus == MapLoadStatus::Loading) {
        stationStateMessage = state.stationsMessage.isEmpty()
                                  ? tr("正在加载附近充电站…")
                                  : state.stationsMessage;
        stationStateKind = QStringLiteral("loading");
        showStationState = true;
    } else if (state.stationsStatus == MapLoadStatus::Empty) {
        stationStateMessage = state.stationsMessage.isEmpty()
                                  ? tr("当前区域暂无充电站。")
                                  : state.stationsMessage;
        stationStateKind = QStringLiteral("empty");
        showStationState = true;
    } else if (state.stationsStatus == MapLoadStatus::Error && !keywordSearch) {
        stationStateMessage = state.searchMessage.isEmpty()
                                  ? tr("附近充电站加载失败。")
                                  : state.searchMessage;
        stationStateKind = QStringLiteral("error");
        showStationState = true;
        showStationRetry = state.canRetryStations;
    }
    ui->stationStateBar->setProperty("state", stationStateKind);
    ui->stationStateLabel->setText(stationStateMessage);
    ui->stationRetryButton->setVisible(showStationRetry);
    ui->stationStateBar->setVisible(showStationState);
    ui->stationStateBar->style()->unpolish(ui->stationStateBar);
    ui->stationStateBar->style()->polish(ui->stationStateBar);

    m_selectedStationId = state.selectedStationId;
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
    QSet<QString> incomingIds;
    QVector<StationListItemView> incomingItems;
    for (const StationListItemView &station : stations) {
        if (station.stationId.isEmpty() || incomingIds.contains(station.stationId))
            continue;
        incomingIds.insert(station.stationId);
        incomingItems.append(station);
    }

    for (auto iterator = m_stationButtons.begin();
         iterator != m_stationButtons.end();) {
        if (incomingIds.contains(iterator.key())) {
            ++iterator;
            continue;
        }
        QPushButton *button = iterator.value();
        ui->stationLayout->removeWidget(button);
        button->deleteLater();
        iterator = m_stationButtons.erase(iterator);
    }

    for (const StationListItemView &station : incomingItems) {
        QPushButton *button = m_stationButtons.value(station.stationId, nullptr);
        if (!button) {
            button = createStationButton(station);
            m_stationButtons.insert(station.stationId, button);
        } else {
            updateStationButton(button, station);
        }
    }
    m_stationItems = incomingItems;
    applyStationOrder(m_selectedStationId);
    ui->stationCountLabel->setText(tr("%1 个").arg(m_stationButtons.size()));
}

QPushButton *MainWindow::createStationButton(const StationListItemView &station)
{
    auto *button = new QPushButton(ui->stationList);
    button->setObjectName(QStringLiteral("stationRow"));
    button->setProperty("stationId", station.stationId);
    button->setProperty("selected", false);
    button->setMinimumHeight(76);

    auto *rowLayout = new QHBoxLayout(button);
    rowLayout->setContentsMargins(10, 7, 8, 7);
    rowLayout->setSpacing(8);

    auto *iconLabel = new QLabel(button);
    iconLabel->setObjectName(QStringLiteral("stationRowIcon"));
    iconLabel->setFixedSize(44, 44);
    iconLabel->setPixmap(QPixmap(QStringLiteral(":/icons/station_charge.png"))
                             .scaled(44, 44, Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation));
    iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    rowLayout->addWidget(iconLabel, 0, Qt::AlignVCenter);

    auto makeLabel = [button](const QString &objectName, const QString &text) {
        auto *label = new QLabel(text, button);
        label->setObjectName(objectName);
        label->setMinimumWidth(0);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        label->setToolTip(text);
        return label;
    };
    QLabel *nameLabel = makeLabel(QStringLiteral("stationRowName"), station.name);
    QLabel *addressLabel = makeLabel(QStringLiteral("stationRowAddress"), station.address);
    QLabel *distanceLabel = makeLabel(QStringLiteral("stationRowDistance"), station.distanceText);
    QLabel *metaLabel = makeLabel(
        QStringLiteral("stationRowMeta"),
        QStringLiteral("%1  ·  %2").arg(station.priceText, station.availabilityText));
    distanceLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    metaLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *textBlock = new QWidget(button);
    textBlock->setMinimumWidth(0);
    textBlock->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *textLayout = new QVBoxLayout(textBlock);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(3);
    textLayout->addWidget(nameLabel);
    textLayout->addWidget(addressLabel);
    rowLayout->addWidget(textBlock, 1);

    auto *detailsButton = new QPushButton(tr("查看详情"), button);
    detailsButton->setObjectName(QStringLiteral("stationDetailsButton"));
    detailsButton->setProperty("stationId", station.stationId);
    detailsButton->setFixedSize(66, 28);

    auto *rightBlock = new QWidget(button);
    rightBlock->setFixedWidth(116);
    auto *rightLayout = new QVBoxLayout(rightBlock);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(2);
    auto *rightTopLayout = new QHBoxLayout;
    rightTopLayout->setContentsMargins(0, 0, 0, 0);
    rightTopLayout->setSpacing(4);
    rightTopLayout->addWidget(distanceLabel, 1);
    rightTopLayout->addWidget(detailsButton, 0);
    rightLayout->addLayout(rightTopLayout);
    rightLayout->addWidget(metaLabel);
    rowLayout->addWidget(rightBlock, 0);
    connect(button, &QPushButton::clicked, this,
            [this, id = station.stationId] { selectStation(id, true); });
    connect(detailsButton, &QPushButton::clicked, this,
            [this, id = station.stationId] { emit stationDetailsRequested(id); });
    return button;
}

void MainWindow::updateStationButton(QPushButton *button,
                                     const StationListItemView &station)
{
    if (!button)
        return;
    auto updateLabel = [button](const QString &objectName, const QString &text) {
        if (QLabel *label = button->findChild<QLabel *>(objectName)) {
            label->setText(text);
            label->setToolTip(text);
        }
    };
    updateLabel(QStringLiteral("stationRowName"), station.name);
    updateLabel(QStringLiteral("stationRowAddress"), station.address);
    updateLabel(QStringLiteral("stationRowDistance"), station.distanceText);
    updateLabel(QStringLiteral("stationRowMeta"),
                QStringLiteral("%1  ·  %2")
                    .arg(station.priceText, station.availabilityText));
}

void MainWindow::applyStationOrder(const QString &selectedStationId)
{
    QVector<StationListItemView> ordered = m_stationItems;
    const auto distanceKey = [](const StationListItemView &station) {
        return station.distanceMeters.value_or(std::numeric_limits<int>::max());
    };
    std::stable_sort(ordered.begin(), ordered.end(),
                     [this, &distanceKey](const StationListItemView &left,
                                          const StationListItemView &right) {
        if (m_stationSortMode == StationSortMode::Availability) {
            const bool leftAvailable = left.availableCount > 0;
            const bool rightAvailable = right.availableCount > 0;
            if (leftAvailable != rightAvailable)
                return leftAvailable;
        }
        return distanceKey(left) < distanceKey(right);
    });

    const auto selected = std::find_if(
        ordered.begin(), ordered.end(),
        [&selectedStationId](const StationListItemView &station) {
            return !selectedStationId.isEmpty()
                   && station.stationId == selectedStationId;
        });
    if (selected != ordered.end() && selected != ordered.begin()) {
        const StationListItemView item = *selected;
        ordered.erase(selected);
        ordered.prepend(item);
    }

    int row = 0;
    for (const StationListItemView &station : ordered) {
        if (QPushButton *button = m_stationButtons.value(station.stationId, nullptr)) {
            ui->stationLayout->removeWidget(button);
            ui->stationLayout->insertWidget(row++, button);
        }
    }
}

void MainWindow::selectStation(const QString &stationId, bool emitIntent)
{
    m_selectedStationId = stationId;
    for (auto it = m_stationButtons.cbegin(); it != m_stationButtons.cend(); ++it) {
        const bool selected = !stationId.isEmpty() && it.key() == stationId;
        it.value()->setProperty("selected", selected);
        if (QLabel *iconLabel = it.value()->findChild<QLabel *>(
                QStringLiteral("stationRowIcon"))) {
            const QString iconPath = selected
                                         ? QStringLiteral(":/icons/station_charge_active.png")
                                         : QStringLiteral(":/icons/station_charge.png");
            iconLabel->setPixmap(QPixmap(iconPath).scaled(
                44, 44, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
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
