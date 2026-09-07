#pragma once

#include "modules/map/maptypes.h"

#include <QObject>
#include <QJsonObject>

/**
 * WebChannel 对外的最小地图桥。
 * 只传递规范化快照和受控事件，不暴露 Service、网络请求或文件访问能力。
 */
class TencentMapBridge final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged)
public:
    explicit TencentMapBridge(QObject *parent = nullptr);

    bool isReady() const { return m_ready; }

    Q_INVOKABLE void reportReady();
    Q_INVOKABLE void reportLoadFailed(const QString &message);
    Q_INVOKABLE void selectStation(const QString &stationId);
    Q_INVOKABLE void reportViewport(const QJsonObject &bounds);

    void setSnapshot(const QJsonObject &snapshot);
    void reset();

signals:
    void readyChanged(bool ready);
    void mapReady();
    void mapLoadFailed(const QString &message);
    void stationSelected(const QString &stationId);
    void viewportChanged(const GeoBounds &bounds);
    void snapshotChanged(const QJsonObject &snapshot);

private:
    bool m_ready = false;
    QJsonObject m_snapshot;
};
