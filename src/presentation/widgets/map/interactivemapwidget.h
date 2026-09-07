#pragma once

#include "modules/map/maptypes.h"

#include <QLabel>
#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>

#include <optional>

class QPushButton;

class InteractiveMapWidget final : public QLabel
{
    Q_OBJECT
public:
    struct Marker { QString stationId; QPointF normalizedPosition; bool available = true; };

    explicit InteractiveMapWidget(QWidget *parent = nullptr);
    void setMarkers(const QList<Marker> &markers);
    void setSelectedStation(const QString &stationId);
    void centerStation(const QString &stationId);
    void fitStations(const QStringList &stationIds);
    void setLocateEnabled(bool enabled);
    void setViewportBounds(const std::optional<GeoBounds> &bounds);
    void reload();

signals:
    void mapReady();
    void markerSelected(const QString &stationId);
    void locateRequested();
    void searchAreaRequested(const GeoBounds &bounds);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QPointF markerPoint(const Marker &marker) const;
    void positionOverlayButtons();
    void clampOffset();

    QList<Marker> m_markers;
    QString m_selectedStationId;
    QPoint m_dragStart;
    QPointF m_dragOriginOffset;
    QPointF m_offset;
    std::optional<GeoBounds> m_viewportBounds;
    bool m_dragging = false;
    bool m_userMoved = false;
    QPushButton *m_locateButton;
    QPushButton *m_searchAreaButton;
};
