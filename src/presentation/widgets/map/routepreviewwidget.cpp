#include "routepreviewwidget.h"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>

RoutePreviewWidget::RoutePreviewWidget(QWidget *parent)
    : QLabel(parent)
{
    setScaledContents(false);
}

void RoutePreviewWidget::setRoutePolyline(const QVector<GeoPoint> &polyline)
{
    m_polyline = polyline;
    update();
}

void RoutePreviewWidget::setOrigin(const std::optional<GeoPoint> &origin)
{
    m_origin = origin;
    update();
}

void RoutePreviewWidget::setDestination(
    const std::optional<GeoPoint> &destination)
{
    m_destination = destination;
    update();
}

void RoutePreviewWidget::clearRoute()
{
    m_polyline.clear();
    m_origin.reset();
    m_destination.reset();
    update();
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
