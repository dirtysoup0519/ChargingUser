#pragma once

#include <QList>
#include <QMetaType>
#include <QString>

/** One station aggregated from the user's authoritative order history. */
struct FrequentStationItemView
{
    QString stationId;
    QString stationName;
    QString lastUsedText;
    int orderCount = 0;
};

struct FrequentStationsViewState
{
    QList<FrequentStationItemView> stations;
    QString message;
};

Q_DECLARE_METATYPE(FrequentStationItemView)
Q_DECLARE_METATYPE(FrequentStationsViewState)
