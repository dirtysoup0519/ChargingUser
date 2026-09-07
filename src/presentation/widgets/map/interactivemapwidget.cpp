#include "interactivemapwidget.h"
#include "tencentmapbridge.h"

#include <QFrame>
#include <QJsonArray>
#include <QJsonObject>
#include <QHBoxLayout>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#ifdef CHARGINGUSER_ENABLE_TENCENT_WEBMAP
#include <QFile>
#include <QWebChannel>
#include <QWebEngineView>
#include <QUrl>
#endif
#include <QVBoxLayout>

InteractiveMapWidget::InteractiveMapWidget(QWidget *parent)
    : QLabel(parent), m_locateButton(new QPushButton(this)),
      m_searchAreaButton(new QPushButton(tr("搜索当前区域"), this)),
      m_mapStatePanel(new QFrame(this)),
      m_mapStateLabel(new QLabel(m_mapStatePanel)),
      m_mapRetryButton(new QPushButton(tr("重新加载地图"), m_mapStatePanel)),
      m_locationStatePanel(new QFrame(this)),
      m_locationStateLabel(new QLabel(m_locationStatePanel)),
      m_locationRetryButton(new QPushButton(tr("重试定位"), m_locationStatePanel))
{
    setCursor(Qt::OpenHandCursor);
    setScaledContents(false);
    m_locateButton->setObjectName(QStringLiteral("btnLocateMap"));
    m_locateButton->setFixedSize(42, 42);
    m_locateButton->setIcon(QIcon(QStringLiteral(":/icons/map_locate.png")));
    m_locateButton->setIconSize(QSize(26, 26));
    m_locateButton->setToolTip(tr("定位到当前位置"));
    m_locateButton->setAccessibleName(tr("定位到当前位置"));
    m_searchAreaButton->setObjectName(QStringLiteral("btnSearchArea"));
    m_searchAreaButton->setFixedSize(126, 38);
    m_searchAreaButton->hide();

    m_mapStatePanel->setObjectName(QStringLiteral("mapStatePanel"));
    m_mapStatePanel->setFixedWidth(252);
    auto *mapStateLayout = new QVBoxLayout(m_mapStatePanel);
    mapStateLayout->setContentsMargins(14, 11, 14, 11);
    mapStateLayout->setSpacing(7);
    m_mapStateLabel->setObjectName(QStringLiteral("mapStateLabel"));
    m_mapStateLabel->setAlignment(Qt::AlignCenter);
    m_mapStateLabel->setWordWrap(true);
    m_mapRetryButton->setObjectName(QStringLiteral("mapRetryButton"));
    m_mapRetryButton->setFixedHeight(30);
    mapStateLayout->addWidget(m_mapStateLabel);
    mapStateLayout->addWidget(m_mapRetryButton);
    m_mapStatePanel->hide();

    m_locationStatePanel->setObjectName(QStringLiteral("locationStatePanel"));
    m_locationStatePanel->setMaximumWidth(270);
    auto *locationStateLayout = new QHBoxLayout(m_locationStatePanel);
    locationStateLayout->setContentsMargins(10, 6, 8, 6);
    locationStateLayout->setSpacing(7);
    m_locationStateLabel->setObjectName(QStringLiteral("locationStateLabel"));
    m_locationStateLabel->setWordWrap(true);
    m_locationRetryButton->setObjectName(QStringLiteral("locationRetryButton"));
    m_locationRetryButton->setFixedSize(68, 28);
    locationStateLayout->addWidget(m_locationStateLabel, 1);
    locationStateLayout->addWidget(m_locationRetryButton);
    m_locationStatePanel->hide();

    connect(m_locateButton, &QPushButton::clicked, this, &InteractiveMapWidget::locateRequested);
    connect(m_searchAreaButton, &QPushButton::clicked, this, [this] {
        m_searchAreaButton->hide();
        if (m_viewportBounds && m_viewportBounds->isValid())
            emit searchAreaRequested(*m_viewportBounds);
    });
    connect(m_mapRetryButton, &QPushButton::clicked,
            this, &InteractiveMapWidget::reload);
    connect(m_locationRetryButton, &QPushButton::clicked,
            this, &InteractiveMapWidget::locateRequested);
    initializeTencentMap();
}

void InteractiveMapWidget::setMarkers(const QList<Marker> &markers)
{
    m_markers = markers;
    if (!m_selectedStationId.isEmpty()) {
        bool found = false;
        for (const Marker &marker : m_markers)
            found = found || marker.stationId == m_selectedStationId;
        if (!found) m_selectedStationId.clear();
    }
#ifdef CHARGINGUSER_ENABLE_TENCENT_WEBMAP
    if (m_mapBridge) {
        QJsonArray items;
        for (const Marker &marker : m_markers) {
            if (!marker.point.isValid()) continue;
            QJsonObject item;
            item.insert(QStringLiteral("stationId"), marker.stationId);
            item.insert(QStringLiteral("latitude"), marker.point.latitude);
            item.insert(QStringLiteral("longitude"), marker.point.longitude);
            item.insert(QStringLiteral("available"), marker.available);
            items.append(item);
        }
        QJsonObject snapshot;
        snapshot.insert(QStringLiteral("markers"), items);
        m_mapBridge->setSnapshot(snapshot);
    }
#endif
    update();
}

void InteractiveMapWidget::setRoutePolyline(const QVector<GeoPoint> &polyline)
{
#ifdef CHARGINGUSER_ENABLE_TENCENT_WEBMAP
    if (m_mapBridge) {
        QJsonArray points;
        for (const GeoPoint &point : polyline) {
            if (!point.isValid()) continue;
            QJsonObject item;
            item.insert(QStringLiteral("latitude"), point.latitude);
            item.insert(QStringLiteral("longitude"), point.longitude);
            points.append(item);
        }
        QJsonObject snapshot;
        QJsonArray markers;
        for (const Marker &marker : m_markers) {
            if (!marker.point.isValid()) continue;
            QJsonObject item;
            item.insert(QStringLiteral("stationId"), marker.stationId);
            item.insert(QStringLiteral("latitude"), marker.point.latitude);
            item.insert(QStringLiteral("longitude"), marker.point.longitude);
            item.insert(QStringLiteral("available"), marker.available);
            markers.append(item);
        }
        snapshot.insert(QStringLiteral("markers"), markers);
        snapshot.insert(QStringLiteral("routePolyline"), points);
        m_mapBridge->setSnapshot(snapshot);
    }
#else
    Q_UNUSED(polyline)
#endif
}

void InteractiveMapWidget::setSelectedStation(const QString &stationId)
{ m_selectedStationId = stationId; update(); }

void InteractiveMapWidget::centerStation(const QString &stationId)
{
    for (const Marker &marker : m_markers) {
        if (marker.stationId != stationId) continue;
        m_offset = QPointF(width() * 0.5
                               - width() * (0.08 + marker.normalizedPosition.x() * 0.84),
                           height() * 0.5
                               - height() * (0.18 + marker.normalizedPosition.y() * 0.72));
        clampOffset(); update(); return;
    }
}

void InteractiveMapWidget::fitStations(const QStringList &stationIds)
{ Q_UNUSED(stationIds) m_offset = QPointF(); update(); }

void InteractiveMapWidget::setLocateEnabled(bool enabled)
{ m_locateButton->setEnabled(enabled); }

void InteractiveMapWidget::renderMapStatus(MapLoadStatus status,
                                            const QString &message,
                                            bool canRetry)
{
    const bool loading = status == MapLoadStatus::Idle
                         || status == MapLoadStatus::Loading;
    const bool failed = status == MapLoadStatus::Error;
    m_mapStatePanel->setVisible(loading || failed);
    if (!loading && !failed)
        return;
    m_mapStatePanel->setProperty("state", failed ? "error" : "loading");
    m_mapStateLabel->setText(message.isEmpty()
                                 ? failed ? tr("地图暂时无法显示。")
                                          : tr("地图加载中…")
                                 : message);
    m_mapRetryButton->setVisible(failed && canRetry);
    m_mapStatePanel->style()->unpolish(m_mapStatePanel);
    m_mapStatePanel->style()->polish(m_mapStatePanel);
    m_mapStatePanel->adjustSize();
    positionOverlayButtons();
}

void InteractiveMapWidget::initializeTencentMap()
{
#ifdef CHARGINGUSER_ENABLE_TENCENT_WEBMAP
    const QString key = qEnvironmentVariable("TENCENT_MAP_KEY");
    if (key.trimmed().isEmpty())
        return; // keep the painter fallback when local credentials are absent
    m_webView = new QWebEngineView(this);
    m_webView->setGeometry(rect());
    m_webView->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_mapBridge = new TencentMapBridge(m_webView);
    auto *channel = new QWebChannel(m_webView);
    channel->registerObject(QStringLiteral("tencentMapBridge"), m_mapBridge);
    m_webView->page()->setWebChannel(channel);
    connect(m_mapBridge, &TencentMapBridge::mapReady, this, &InteractiveMapWidget::mapReady);
    connect(m_mapBridge, &TencentMapBridge::stationSelected, this, &InteractiveMapWidget::markerSelected);
    QFile file(QStringLiteral(":/map/tencent-map.html"));
    if (!file.open(QIODevice::ReadOnly)) {
        m_mapBridge->reportLoadFailed(QStringLiteral("腾讯地图资源加载失败"));
        return;
    }
    QString html = QString::fromUtf8(file.readAll());
    html.replace(QStringLiteral("__TENCENT_KEY__"), QString::fromUtf8(QUrl::toPercentEncoding(key)));
    m_webView->setHtml(html, QUrl(QStringLiteral("qrc:///map/")));
    m_webView->show();
#endif
}

void InteractiveMapWidget::renderLocationStatus(MapLoadStatus status,
                                                 const QString &message,
                                                 bool canRetry)
{
    const bool loading = status == MapLoadStatus::Loading;
    const bool failed = status == MapLoadStatus::Error;
    m_locationStatePanel->setVisible(loading || failed);
    if (!loading && !failed)
        return;
    m_locationStatePanel->setProperty("state", failed ? "error" : "loading");
    m_locationStateLabel->setText(message.isEmpty()
                                      ? failed ? tr("无法获取当前位置。")
                                               : tr("正在获取当前位置…")
                                      : message);
    m_locationRetryButton->setVisible(failed && canRetry);
    m_locationStatePanel->style()->unpolish(m_locationStatePanel);
    m_locationStatePanel->style()->polish(m_locationStatePanel);
    m_locationStatePanel->adjustSize();
    positionOverlayButtons();
}

void InteractiveMapWidget::setViewportBounds(
    const std::optional<GeoBounds> &bounds)
{
    m_viewportBounds = bounds && bounds->isValid() ? bounds : std::nullopt;
    if (!m_viewportBounds)
        m_searchAreaButton->hide();
}

void InteractiveMapWidget::reload()
{
    update();
    QTimer::singleShot(0, this, [this] { emit mapReady(); });
}

void InteractiveMapWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return QLabel::mousePressEvent(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPointF pointer = event->position();
#else
    const QPointF pointer = event->localPos();
#endif
    for (const Marker &marker : m_markers) {
        if (markerRect(marker).adjusted(-4, -4, 4, 4).contains(pointer)) {
            emit markerSelected(marker.stationId); return;
        }
    }
    m_dragStart = event->pos();
    m_dragOriginOffset = m_offset;
    m_dragging = true; m_userMoved = false;
    setCursor(Qt::ClosedHandCursor);
}

void InteractiveMapWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging) return;
    const QPoint delta = event->pos() - m_dragStart;
    if (delta.manhattanLength() > 2) m_userMoved = true;
    m_offset += delta; m_dragStart = event->pos(); clampOffset(); update();
}

void InteractiveMapWidget::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
    if (!m_dragging) return;
    m_dragging = false; setCursor(Qt::OpenHandCursor);
    if (!m_userMoved || !m_viewportBounds)
        return;

    const QPointF delta = m_offset - m_dragOriginOffset;
    const double latitudeSpan = m_viewportBounds->northEast.latitude
                                - m_viewportBounds->southWest.latitude;
    const double longitudeSpan = m_viewportBounds->northEast.longitude
                                 - m_viewportBounds->southWest.longitude;
    const double latitudeShift = delta.y() / qMax(1, height()) * latitudeSpan;
    const double longitudeShift = -delta.x() / qMax(1, width()) * longitudeSpan;
    GeoBounds shifted = *m_viewportBounds;
    shifted.southWest.latitude += latitudeShift;
    shifted.northEast.latitude += latitudeShift;
    shifted.southWest.longitude += longitudeShift;
    shifted.northEast.longitude += longitudeShift;
    if (shifted.isValid()) {
        m_viewportBounds = shifted;
        m_searchAreaButton->show();
    }
}

void InteractiveMapWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    const QPixmap map = pixmap(Qt::ReturnByValue);
#else
    const QPixmap map = pixmap() ? *pixmap() : QPixmap();
#endif
    if (!map.isNull()) {
        const QSizeF scaled(width() * 1.28, height() * 1.28);
        const QRectF target((width() - scaled.width()) / 2.0 + m_offset.x(),
                            (height() - scaled.height()) / 2.0 + m_offset.y(),
                            scaled.width(), scaled.height());
        painter.drawPixmap(target, map, map.rect());
    } else painter.fillRect(rect(), QColor(QStringLiteral("#EEF4F8")));
    static const QPixmap availableMarker(
        QStringLiteral(":/icons/map_station_marker.png"));
    static const QPixmap selectedMarker(
        QStringLiteral(":/icons/map_station_marker_active.png"));
    static const QPixmap unavailableMarker(
        QStringLiteral(":/icons/map_station_marker_unavailable.png"));
    for (const Marker &marker : m_markers) {
        const bool selected = marker.stationId == m_selectedStationId;
        const QPixmap &markerPixmap = selected
                                          ? selectedMarker
                                          : marker.available
                                                ? availableMarker
                                                : unavailableMarker;
        painter.drawPixmap(markerRect(marker), markerPixmap,
                           QRectF(markerPixmap.rect()));
    }
}

void InteractiveMapWidget::resizeEvent(QResizeEvent *event)
{ QLabel::resizeEvent(event); clampOffset(); positionOverlayButtons(); }

QPointF InteractiveMapWidget::markerPoint(const Marker &marker) const
{ return QPointF(width() * (0.08 + marker.normalizedPosition.x() * 0.84) + m_offset.x(), height() * (0.18 + marker.normalizedPosition.y() * 0.72) + m_offset.y()); }

QRectF InteractiveMapWidget::markerRect(const Marker &marker) const
{
    const bool selected = marker.stationId == m_selectedStationId;
    const QSizeF size = selected ? QSizeF(46, 55) : QSizeF(40, 48);
    const QPointF anchor = markerPoint(marker);
    return QRectF(qRound(anchor.x() - size.width() / 2.0),
                  qRound(anchor.y() - size.height()),
                  size.width(), size.height());
}

void InteractiveMapWidget::positionOverlayButtons()
{
    m_locateButton->move(width() - m_locateButton->width() - 14, height() - m_locateButton->height() - 14);
    m_searchAreaButton->move((width() - m_searchAreaButton->width()) / 2, height() - m_searchAreaButton->height() - 16);
    m_mapStatePanel->move((width() - m_mapStatePanel->width()) / 2,
                          (height() - m_mapStatePanel->height()) / 2);
    m_locationStatePanel->move(12, 12);
    m_mapStatePanel->raise();
    m_locationStatePanel->raise();
    m_locateButton->raise();
    m_searchAreaButton->raise();
}

void InteractiveMapWidget::clampOffset()
{
    const qreal maxX = width() * 0.14, maxY = height() * 0.14;
    m_offset.setX(qBound(-maxX, m_offset.x(), maxX));
    m_offset.setY(qBound(-maxY, m_offset.y(), maxY));
}
