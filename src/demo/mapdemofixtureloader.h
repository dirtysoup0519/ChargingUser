#pragma once

#include "modules/charger/chargertypes.h"
#include "modules/map/maptypes.h"

#include <QString>
#include <QVector>

struct MapDemoBehavior
{
    QString outcome = QStringLiteral("success");
    int responseDelayMs = 650;
    int timeoutMs = 2500;
    QString errorCode;
    QString errorMessage;
    bool retryable = true;
};

struct MapDemoFixture
{
    LocationResult location;
    QVector<StationDetail> stations;
    QString canvasState = QStringLiteral("ready");
    MapDemoBehavior locationBehavior;
    MapDemoBehavior stationsBehavior;
};

/** Loads map-only Demo data. This parser is not used by production services. */
bool loadMapDemoFixture(const QString &resourcePath,
                        MapDemoFixture *fixture,
                        QString *errorMessage = nullptr);
