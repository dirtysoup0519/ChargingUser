#pragma once

#include "presentation/contracts/mapviewstates.h"

#include <QLabel>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <optional>

class QPushButton;
class QFrame;
class QWebEngineView;
class QShowEvent;
class TencentMapBridge;

class InteractiveMapWidget final : public QLabel
{
    Q_OBJECT
public:
    struct Marker { QString stationId; QPointF normalizedPosition; bool available = true; GeoPoint point; };

    explicit InteractiveMapWidget(QWidget *parent = nullptr);
    void setMapKey(const QString &key);
    void setMarkers(const QList<Marker> &markers);
    void setRoutePolyline(const QVector<GeoPoint> &polyline);
    void setSelectedStation(const QString &stationId);
    void centerStation(const QString &stationId);
    void fitStations(const QStringList &stationIds);
    void setLocateEnabled(bool enabled);
    void renderMapStatus(MapLoadStatus status,
                         const QString &message,
                         bool canRetry);
    void renderLocationStatus(MapLoadStatus status,
                              const QString &message,
                              bool canRetry);
    void setViewportBounds(const std::optional<GeoBounds> &bounds);
    void reload();

signals:
    void mapReady();
    void mapLoadFailed();
    void markerSelected(const QString &stationId);
    void locateRequested();
    void searchAreaRequested(const GeoBounds &bounds);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void initializeTencentMap();
    QPointF markerPoint(const Marker &marker) const;
    QRectF markerRect(const Marker &marker) const;
    void positionOverlayButtons();
    void clampOffset();

    QList<Marker> m_markers;
    QString m_selectedStationId;
    QPoint m_dragStart;
    QPointF m_dragOriginOffset;
    QPointF m_offset;
    std::optional<GeoBounds> m_viewportBounds;
    QString m_mapKey;
    bool m_dragging = false;
    bool m_userMoved = false;
    QPushButton *m_locateButton;
    QPushButton *m_searchAreaButton;
    QFrame *m_mapStatePanel;
    QLabel *m_mapStateLabel;
    QPushButton *m_mapRetryButton;
    QFrame *m_locationStatePanel;
    QLabel *m_locationStateLabel;
    QPushButton *m_locationRetryButton;
    QWebEngineView *m_webView = nullptr;
    TencentMapBridge *m_mapBridge = nullptr;
};
