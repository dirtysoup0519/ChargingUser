#include "routepreviewwidget.h"
#include "tencentmapbridge.h"

#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTimer>
#include <algorithm>
#include <cmath>
#ifdef CHARGINGUSER_ENABLE_TENCENT_WEBMAP
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QWebChannel>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QUrl>
#endif

RoutePreviewWidget::RoutePreviewWidget(QWidget *parent)
    : QLabel(parent), m_recenterButton(new QPushButton(this))
{
    setScaledContents(false);
    m_recenterButton->setObjectName(QStringLiteral("btnLocateMap"));
    m_recenterButton->setFixedSize(42, 42);
    m_recenterButton->setIcon(QIcon(QStringLiteral(":/icons/map_locate.png")));
    m_recenterButton->setIconSize(QSize(26, 26));
    m_recenterButton->setToolTip(tr("回到路线起点并显示完整路线"));
    m_recenterButton->setAccessibleName(tr("回到路线中心"));
    m_recenterButton->hide();
    connect(m_recenterButton, &QPushButton::clicked,
            this, &RoutePreviewWidget::recenterRoute);
}

void RoutePreviewWidget::initializeTencentMap()
{
#ifdef CHARGINGUSER_ENABLE_TENCENT_WEBMAP
    const QString key = qEnvironmentVariable("TENCENT_MAP_KEY");
    if (key.trimmed().isEmpty())
        return;
    m_webView = new QWebEngineView(this);
    m_webView->setGeometry(rect());
    m_webView->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled,
                                        true);
    m_webView->settings()->setAttribute(QWebEngineSettings::WebGLEnabled,
                                        true);
    m_mapBridge = new TencentMapBridge(m_webView);
    auto *channel = new QWebChannel(m_webView);
    channel->registerObject(QStringLiteral("tencentMapBridge"), m_mapBridge);
    m_webView->page()->setWebChannel(channel);
    connect(m_mapBridge, &TencentMapBridge::mapReady, this, [this] {
        m_webView->show();
        m_recenterButton->raise();
    });
    connect(m_mapBridge, &TencentMapBridge::mapLoadFailed,
            m_webView, &QWebEngineView::hide);
    QFile file(QStringLiteral(":/map/tencent-map.html"));
    if (!file.open(QIODevice::ReadOnly)) return;
    QString html = QString::fromUtf8(file.readAll());
    html.replace(QStringLiteral("__TENCENT_KEY__"), QString::fromUtf8(QUrl::toPercentEncoding(key)));
    publishSnapshot();
    m_webView->show();
    m_webView->setHtml(html, QUrl(QStringLiteral("https://localhost/")));
    m_recenterButton->raise();
#endif
}

void RoutePreviewWidget::setRoutePolyline(const QVector<GeoPoint> &polyline)
{
    m_polyline = polyline;
    publishSnapshot();
    updateRecenterButton();
    update();
}

void RoutePreviewWidget::setOrigin(const std::optional<GeoPoint> &origin)
{
    m_origin = origin;
    publishSnapshot();
    updateRecenterButton();
    update();
}

void RoutePreviewWidget::setDestination(
    const std::optional<GeoPoint> &destination)
{
    m_destination = destination;
    publishSnapshot();
    updateRecenterButton();
    update();
}

void RoutePreviewWidget::clearRoute()
{
    m_polyline.clear();
    m_origin.reset();
    m_destination.reset();
    publishSnapshot();
    updateRecenterButton();
    update();
}

void RoutePreviewWidget::publishSnapshot()
{
#ifdef CHARGINGUSER_ENABLE_TENCENT_WEBMAP
    if (!m_mapBridge)
        return;

    QJsonArray points;
    for (const GeoPoint &point : m_polyline) {
        if (!point.isValid())
            continue;
        points.append(QJsonObject{
            {QStringLiteral("latitude"), point.latitude},
            {QStringLiteral("longitude"), point.longitude}});
    }

    QJsonArray markers;
    const auto appendEndpoint = [&markers](const QString &id,
                                            const std::optional<GeoPoint> &point) {
        if (!point || !point->isValid())
            return;
        markers.append(QJsonObject{
            {QStringLiteral("stationId"), id},
            {QStringLiteral("latitude"), point->latitude},
            {QStringLiteral("longitude"), point->longitude},
            {QStringLiteral("available"), true}});
    };
    appendEndpoint(QStringLiteral("route-origin"), m_origin);
    appendEndpoint(QStringLiteral("route-destination"), m_destination);

    QJsonObject snapshot;
    snapshot.insert(QStringLiteral("markers"), markers);
    snapshot.insert(QStringLiteral("routePolyline"), points);
    snapshot.insert(QStringLiteral("selectedStationId"),
                    QStringLiteral("route-origin"));
    m_mapBridge->setSnapshot(snapshot);
#endif
}

void RoutePreviewWidget::updateRecenterButton()
{
    const int validPointCount = std::count_if(
        m_polyline.cbegin(), m_polyline.cend(),
        [](const GeoPoint &point) { return point.isValid(); });
    const bool canRecenter = m_origin && m_origin->isValid()
                             && validPointCount > 1;
    m_recenterButton->setVisible(canRecenter);
    m_recenterButton->move(qMax(8, width() - m_recenterButton->width() - 10),
                           qMax(8, height() - m_recenterButton->height() - 10));
    if (canRecenter)
        m_recenterButton->raise();
}

void RoutePreviewWidget::recenterRoute()
{
    // Republishing the current route makes the Web map reapply the same
    // origin-centred camera calculation without issuing a route request.
    publishSnapshot();
    update();
    m_recenterButton->raise();
}

void RoutePreviewWidget::resizeEvent(QResizeEvent *event)
{
    QLabel::resizeEvent(event);
#ifdef CHARGINGUSER_ENABLE_TENCENT_WEBMAP
    if (m_webView) m_webView->setGeometry(rect());
#endif
    updateRecenterButton();
}

void RoutePreviewWidget::showEvent(QShowEvent *event)
{
    QLabel::showEvent(event);
#ifdef CHARGINGUSER_ENABLE_TENCENT_WEBMAP
    if (!m_webView && !qEnvironmentVariableIsEmpty("TENCENT_MAP_KEY")) {
        QTimer::singleShot(0, this, [this] {
            if (isVisible() && !m_webView)
                initializeTencentMap();
        });
    }
#endif
}

void RoutePreviewWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    const QPixmap background = pixmap(Qt::ReturnByValue);
#else
    const QPixmap background = pixmap() ? *pixmap() : QPixmap();
#endif
    if (!background.isNull())
        painter.drawPixmap(rect(), background, background.rect());
    else
        painter.fillRect(rect(), QColor(QStringLiteral("#EEF4F8")));

    QVector<GeoPoint> validPoints;
    for (const GeoPoint &point : m_polyline) {
        if (point.isValid())
            validPoints.append(point);
    }
    if (m_origin && m_origin->isValid())
        validPoints.append(*m_origin);
    if (m_destination && m_destination->isValid())
        validPoints.append(*m_destination);
    if (validPoints.isEmpty())
        return;

    double minLatitude = validPoints.first().latitude;
    double maxLatitude = minLatitude;
    double minLongitude = validPoints.first().longitude;
    double maxLongitude = minLongitude;
    for (const GeoPoint &point : validPoints) {
        minLatitude = std::min(minLatitude, point.latitude);
        maxLatitude = std::max(maxLatitude, point.latitude);
        minLongitude = std::min(minLongitude, point.longitude);
        maxLongitude = std::max(maxLongitude, point.longitude);
    }

    // Keep the origin in the visual center while expanding the bounds just
    // enough to include the complete route in every direction.
    if (m_origin && m_origin->isValid()) {
        const double latitudeRadius = std::max(
            std::abs(maxLatitude - m_origin->latitude),
            std::abs(m_origin->latitude - minLatitude));
        const double longitudeRadius = std::max(
            std::abs(maxLongitude - m_origin->longitude),
            std::abs(m_origin->longitude - minLongitude));
        minLatitude = m_origin->latitude - latitudeRadius;
        maxLatitude = m_origin->latitude + latitudeRadius;
        minLongitude = m_origin->longitude - longitudeRadius;
        maxLongitude = m_origin->longitude + longitudeRadius;
    }

    const QRectF drawArea = rect().adjusted(32, 28, -32, -28);
    QPainterPath path;
    bool started = false;
    for (const GeoPoint &point : m_polyline) {
        if (!point.isValid())
            continue;
        const QPointF displayPoint = mapPoint(
            point, drawArea, minLatitude, maxLatitude,
            minLongitude, maxLongitude);
        if (!started) {
            path.moveTo(displayPoint);
            started = true;
        } else {
            path.lineTo(displayPoint);
        }
    }
    if (started) {
        painter.setPen(QPen(QColor(QStringLiteral("#FFFFFF")), 8,
                            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
        painter.setPen(QPen(QColor(QStringLiteral("#1677FF")), 5,
                            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
    }

    const auto drawEndpoint = [&](const std::optional<GeoPoint> &point,
                                  const QColor &color) {
        if (!point || !point->isValid())
            return;
        const QPointF displayPoint = mapPoint(
            *point, drawArea, minLatitude, maxLatitude,
            minLongitude, maxLongitude);
        painter.setPen(QPen(Qt::white, 3));
        painter.setBrush(color);
        painter.drawEllipse(displayPoint, 10, 10);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawEllipse(displayPoint, 3, 3);
    };
    drawEndpoint(m_origin, QColor(QStringLiteral("#18B8AD")));
    drawEndpoint(m_destination, QColor(QStringLiteral("#F43F5E")));
}

QPointF RoutePreviewWidget::mapPoint(
    const GeoPoint &point, const QRectF &area,
    double minLatitude, double maxLatitude,
    double minLongitude, double maxLongitude) const
{
    const double latitudeSpan = std::max(0.000001, maxLatitude - minLatitude);
    const double longitudeSpan = std::max(0.000001, maxLongitude - minLongitude);
    const double x = (point.longitude - minLongitude) / longitudeSpan;
    const double y = 1.0 - (point.latitude - minLatitude) / latitudeSpan;
    return QPointF(area.left() + x * area.width(),
                   area.top() + y * area.height());
}
