#pragma once

#include "modules/map/maptypes.h"

#include <QLabel>
#include <QVector>
#include <optional>

class QWebEngineView;
class QPushButton;
class QShowEvent;
class TencentMapBridge;

/** 只负责绘制已规范化的路线数据；不调用地图供应商或业务服务。 */
class RoutePreviewWidget final : public QLabel
{
    Q_OBJECT

public:
    explicit RoutePreviewWidget(QWidget *parent = nullptr);

    void setRoutePolyline(const QVector<GeoPoint> &polyline);
    void setOrigin(const std::optional<GeoPoint> &origin);
    void setDestination(const std::optional<GeoPoint> &destination);
    void clearRoute();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void initializeTencentMap();
    void publishSnapshot();
    void updateRecenterButton();
    void recenterRoute();
    QPointF mapPoint(const GeoPoint &point, const QRectF &area,
                     double minLatitude, double maxLatitude,
                     double minLongitude, double maxLongitude) const;

    QVector<GeoPoint> m_polyline;
    std::optional<GeoPoint> m_origin;
    std::optional<GeoPoint> m_destination;
    QWebEngineView *m_webView = nullptr;
    TencentMapBridge *m_mapBridge = nullptr;
    QPushButton *m_recenterButton;
};
