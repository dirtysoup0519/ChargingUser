#pragma once

#include <QLabel>
#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>

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

signals:
    void markerSelected(const QString &stationId);
    void locateRequested();
    void searchAreaRequested();

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
    QPointF m_offset;
    bool m_dragging = false;
    bool m_userMoved = false;
    QPushButton *m_locateButton;
    QPushButton *m_searchAreaButton;
};
