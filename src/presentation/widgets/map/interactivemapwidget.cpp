#include "interactivemapwidget.h"

#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTimer>

InteractiveMapWidget::InteractiveMapWidget(QWidget *parent)
    : QLabel(parent), m_locateButton(new QPushButton(tr("⌖"), this)),
      m_searchAreaButton(new QPushButton(tr("搜索当前区域"), this))
{
    setCursor(Qt::OpenHandCursor);
    setScaledContents(false);
    m_locateButton->setObjectName(QStringLiteral("btnLocateMap"));
    m_locateButton->setFixedSize(42, 42);
    m_searchAreaButton->setObjectName(QStringLiteral("btnSearchArea"));
    m_searchAreaButton->setFixedSize(126, 38);
    m_searchAreaButton->hide();
    connect(m_locateButton, &QPushButton::clicked, this, &InteractiveMapWidget::locateRequested);
    connect(m_searchAreaButton, &QPushButton::clicked, this, [this] {
        m_searchAreaButton->hide();
        if (m_viewportBounds && m_viewportBounds->isValid())
            emit searchAreaRequested(*m_viewportBounds);
    });
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
    update();
}

void InteractiveMapWidget::setSelectedStation(const QString &stationId)
{ m_selectedStationId = stationId; update(); }

void InteractiveMapWidget::centerStation(const QString &stationId)
{
    for (const Marker &marker : m_markers) {
        if (marker.stationId != stationId) continue;
        m_offset = QPointF((0.5 - marker.normalizedPosition.x()) * width() * 0.84,
                           (0.5 - marker.normalizedPosition.y()) * height() * 0.84);
        clampOffset(); update(); return;
    }
}

void InteractiveMapWidget::fitStations(const QStringList &stationIds)
{ Q_UNUSED(stationIds) m_offset = QPointF(); update(); }

void InteractiveMapWidget::setLocateEnabled(bool enabled)
{ m_locateButton->setEnabled(enabled); }

void InteractiveMapWidget::setViewportBounds(const std::optional<GeoBounds> &bounds)
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
        if (QLineF(pointer, markerPoint(marker)).length() <= 24.0) {
            emit markerSelected(marker.stationId); return;
        }
    }
    m_dragStart = event->pos(); m_dragOriginOffset = m_offset;
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
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
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
    for (const Marker &marker : m_markers) {
        const QPointF point = markerPoint(marker);
        const bool selected = marker.stationId == m_selectedStationId;
        painter.setPen(QPen(Qt::white, 2));
        painter.setBrush(selected ? QColor("#1677FF") : marker.available ? QColor("#18B7A0") : QColor("#A0A8B4"));
        painter.drawEllipse(point, selected ? 17 : 13, selected ? 17 : 13);
        painter.setPen(QPen(Qt::white, 3)); painter.drawLine(point + QPointF(-4, 3), point + QPointF(1, -5));
        painter.drawLine(point + QPointF(1, -5), point + QPointF(5, 2));
    }
}

void InteractiveMapWidget::resizeEvent(QResizeEvent *event)
{ QLabel::resizeEvent(event); clampOffset(); positionOverlayButtons(); }

QPointF InteractiveMapWidget::markerPoint(const Marker &marker) const
{ return QPointF(width() * (0.08 + marker.normalizedPosition.x() * 0.84) + m_offset.x(), height() * (0.08 + marker.normalizedPosition.y() * 0.84) + m_offset.y()); }

void InteractiveMapWidget::positionOverlayButtons()
{
    m_locateButton->move(width() - m_locateButton->width() - 14, height() - m_locateButton->height() - 14);
    m_searchAreaButton->move((width() - m_searchAreaButton->width()) / 2, height() - m_searchAreaButton->height() - 16);
    m_locateButton->raise(); m_searchAreaButton->raise();
}

void InteractiveMapWidget::clampOffset()
{
    const qreal maxX = width() * 0.14, maxY = height() * 0.14;
    m_offset.setX(qBound(-maxX, m_offset.x(), maxX));
    m_offset.setY(qBound(-maxY, m_offset.y(), maxY));
}
