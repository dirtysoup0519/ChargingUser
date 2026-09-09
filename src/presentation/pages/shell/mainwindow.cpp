#include "mainwindow.h"
#include "avatarimagehelper.h"
#include "dragscrollhelper.h"
#include "ui_mainwindow.h"
#include "interactivemapwidget.h"
#include <algorithm>
#include <limits>
#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QListWidgetItem>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QWidget(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->chargingSessionWidget->setEmbeddedMode(true);
    DragScrollHelper::enableFor(this);

    m_homeReservationCard = new QFrame(ui->homePage);
    m_homeReservationCard->setObjectName(QStringLiteral("homeReservationCard"));
    m_homeReservationCard->setCursor(Qt::PointingHandCursor);
    m_homeReservationCard->installEventFilter(this);
    auto *reservationLayout = new QHBoxLayout(m_homeReservationCard);
    reservationLayout->setContentsMargins(14, 8, 14, 8);
    reservationLayout->setSpacing(10);
    auto *reservationText = new QWidget(m_homeReservationCard);
    reservationText->setObjectName(QStringLiteral("homeReservationText"));
    reservationText->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *reservationTextLayout = new QVBoxLayout(reservationText);
    reservationTextLayout->setContentsMargins(0, 0, 0, 0);
    reservationTextLayout->setSpacing(2);
    m_homeReservationTitle = new QLabel(reservationText);
    m_homeReservationTitle->setObjectName(QStringLiteral("homeReservationTitle"));
    m_homeReservationStation = new QLabel(reservationText);
    m_homeReservationStation->setObjectName(QStringLiteral("homeReservationStation"));
    m_homeReservationStation->setTextInteractionFlags(Qt::NoTextInteraction);
    reservationTextLayout->addWidget(m_homeReservationTitle);
    reservationTextLayout->addWidget(m_homeReservationStation);
    reservationLayout->addWidget(reservationText, 1);
    m_homeReservationCountdown = new QLabel(m_homeReservationCard);
    m_homeReservationCountdown->setObjectName(QStringLiteral("homeReservationCountdown"));
    m_homeReservationCountdown->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_homeReservationCountdown->setAttribute(Qt::WA_TransparentForMouseEvents);
    reservationLayout->addWidget(m_homeReservationCountdown);
    auto *chevron = new QLabel(QStringLiteral("›"), m_homeReservationCard);
    chevron->setObjectName(QStringLiteral("homeReservationChevron"));
    chevron->setAttribute(Qt::WA_TransparentForMouseEvents);
    reservationLayout->addWidget(chevron);
    ui->homeLayout->insertWidget(1, m_homeReservationCard);
    m_homeReservationCard->hide();
    m_homeReservationTimer = new QTimer(this);
    m_homeReservationTimer->setInterval(1000);
    connect(m_homeReservationTimer, &QTimer::timeout,
            this, &MainWindow::updateHomeReservationCountdown);

    connect(ui->mapView, &InteractiveMapWidget::markerSelected,
            this, [this](const QString &stationId) { selectStation(stationId, true); });
    connect(ui->mapView, &InteractiveMapWidget::locateRequested,
            this, [this] {
        // 先立即给出视野反馈；定位服务返回后 Binder 会刷新站点数据。
        // 若已有选中站点，将其重新置中，避免点击按钮没有可见响应。
        const QString focusId = !m_selectedStationId.isEmpty()
                                    ? m_selectedStationId
                                    : (m_stationItems.isEmpty()
                                           ? QString()
                                           : m_stationItems.first().stationId);
        if (!focusId.isEmpty())
            ui->mapView->centerStation(focusId);
        emit locateRequested();
    });
    connect(ui->mapView, &InteractiveMapWidget::searchAreaRequested,
            this, &MainWindow::searchAreaRequested);
    connect(ui->mapView, &InteractiveMapWidget::mapReady,
            this, &MainWindow::mapReady);
    connect(ui->mapView, &InteractiveMapWidget::mapLoadFailed,
            this, &MainWindow::mapLoadFailed);
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
            this, &MainWindow::resetStationSearch);
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
        // “充电”标签是扫码入口；活动充电会话只由启动成功、订单恢复或
        // 订单列表查看进入，避免用户从主页误打开旧会话页面。
        emit scanChargingRequested();
    });
    connect(ui->profileNav, &QToolButton::clicked, this, [this] {
        emit primaryPageRequested(PrimaryPage::Profile);
    });
    connect(ui->btnEditProfile, &QToolButton::clicked,
            this, &MainWindow::profileEditRequested);
    connect(ui->btnRecharge, &QPushButton::clicked,
            this, &MainWindow::rechargePageRequested);
    connect(ui->profileMenuList, &QListWidget::itemClicked,
            this, [this](QListWidgetItem *item) {
        if (!item) return;
        switch (ui->profileMenuList->row(item)) {
        case 0: emit ordersPageRequested(); break;
        case 1:
            emit frequentStationsRequested();
            emit commonStationsPageRequested();
            break;
        case 2:
            emit feedbackRequested();
            emit helpFeedbackPageRequested();
            break;
        case 3:
            emit aboutRequested();
            emit aboutPageRequested();
            break;
        default: break;
        }
    });
    connect(ui->btnLogout, &QPushButton::clicked,
            this, &MainWindow::logoutRequested);
    connect(ui->chargingSessionWidget, &ChargingSessionWindow::scanChargingRequested,
            this, &MainWindow::scanChargingRequested);
    connect(ui->chargingSessionWidget, &ChargingSessionWindow::activeSessionsRequested,
            this, &MainWindow::activeSessionsRequested);
    connect(ui->chargingSessionWidget, &ChargingSessionWindow::activeSessionSelected,
            this, &MainWindow::activeSessionSelected);
    connect(ui->chargingSessionWidget, &ChargingSessionWindow::refreshRequested,
            this, &MainWindow::chargingRefreshRequested);
    connect(ui->chargingSessionWidget, &ChargingSessionWindow::stopChargingRequested,
            this, &MainWindow::stopChargingRequested);
    connect(ui->chargingSessionWidget, &ChargingSessionWindow::recoverStopResultRequested,
            this, &MainWindow::recoverStopResultRequested);
}

void MainWindow::renderChargingSession(const ChargingSessionViewState &state)
{
    ui->chargingSessionWidget->render(state);
}

void MainWindow::renderChargingSessions(
    const ChargingSessionCollectionViewState &state)
{
    ui->chargingSessionWidget->renderSessions(state);
}

void MainWindow::resetStationSearch()
{
    ui->searchBox->clear();
    ui->searchBox->clearFocus();
    emit stationSearchCleared();
}

void MainWindow::setMapKey(const QString &key)
{
    if (ui && ui->mapView)
        ui->mapView->setMapKey(key);
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::renderHome(const HomeMapViewState &state)
{
    m_homeActiveReservation = state.activeReservation;
    const bool hasReservation = m_homeActiveReservation.has_value();
    m_homeReservationCard->setVisible(hasReservation);
    ui->mapView->setFixedHeight(hasReservation ? 296 : 354);
    if (hasReservation) {
        const QString chargerCode = m_homeActiveReservation->chargerId
                                        .section(QLatin1Char('-'), -1).toUpper();
        m_homeReservationTitle->setText(tr("已预约 · %1号桩").arg(chargerCode));
        QString stationName;
        for (const StationListItemView &station : state.stations) {
            if (station.stationId == m_homeActiveReservation->stationId) {
                stationName = station.name;
                break;
            }
        }
        m_homeReservationStation->setText(
            stationName.isEmpty() ? tr("点击查看预约所属站点") : stationName);
        updateHomeReservationCountdown();
        m_homeReservationTimer->start();
    } else {
        m_homeReservationTimer->stop();
    }
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
    ui->mapView->setViewportBounds(state.camera.bounds);

    QString stationStateMessage;
    QString stationStateKind;
    bool showStationState = false;
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
        stationStateMessage = state.stationsMessage.isEmpty()
                                  ? tr("附近充电站加载失败。")
                                  : state.stationsMessage;
        stationStateKind = QStringLiteral("error");
        showStationState = true;
    }
    // Keep the station toolbar visible after a successful load so the user can
    // manually refresh without first waiting for an error state.
    if (!state.stations.isEmpty()) {
        showStationState = true;
        if (stationStateMessage.isEmpty())
            stationStateMessage = tr("附近充电站");
        stationStateKind = QStringLiteral("ready");
    }
    ui->stationStateBar->setProperty("state", stationStateKind);
    ui->stationStateLabel->setText(stationStateMessage);
    ui->stationRetryButton->setText(state.stationsStatus == MapLoadStatus::Loading
                                        ? tr("刷新中…") : tr("刷新"));
    ui->stationRetryButton->setEnabled(state.stationsStatus != MapLoadStatus::Loading);
    ui->stationRetryButton->setVisible(showStationState || !state.stations.isEmpty());
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
                            true,
                            marker.point});
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

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_homeReservationCard && event->type() == QEvent::MouseButtonPress
        && m_homeActiveReservation) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() != Qt::LeftButton) return true;
        const QString chargerCode = m_homeActiveReservation->chargerId
                                        .section(QLatin1Char('-'), -1).toUpper();
        if (QMessageBox::question(
                this, tr("查看已有预约"),
                tr("是否前往预约所属站点并查看充电桩 %1？").arg(chargerCode),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes)
            == QMessageBox::Yes) {
            emit activeReservationRequested(m_homeActiveReservation->reservationId,
                                            m_homeActiveReservation->stationId,
                                            m_homeActiveReservation->chargerId);
        }
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void MainWindow::updateHomeReservationCountdown()
{
    if (!m_homeActiveReservation || !m_homeReservationCard->isVisible()) {
        m_homeReservationTimer->stop();
        return;
    }
    qint64 seconds = QDateTime::currentDateTimeUtc().secsTo(
        m_homeActiveReservation->expiresAtUtc);
    seconds = qMax<qint64>(0, seconds);
    m_homeReservationCountdown->setText(
        tr("剩余 %1:%2").arg(seconds / 60, 2, 10, QLatin1Char('0'))
                           .arg(seconds % 60, 2, 10, QLatin1Char('0')));
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
    const QPixmap avatar = AvatarImageHelper::pixmapFromDataUri(state.avatarDataUri);
    const QPixmap source = avatar.isNull()
                               ? QPixmap(QStringLiteral(":/icons/default_avatar.png"))
                               : avatar;
    ui->profileAvatarLabel->setPixmap(source.scaled(
        ui->profileAvatarLabel->size(), Qt::KeepAspectRatioByExpanding,
        Qt::SmoothTransformation));
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
            if (left.availableCount != right.availableCount)
                return left.availableCount > right.availableCount;
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
