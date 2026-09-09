#include "demo/mapdemofixtureloader.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace {

bool fail(QString *errorMessage, const QString &message)
{
    if (errorMessage)
        *errorMessage = message;
    return false;
}

bool readPoint(const QJsonObject &object, GeoPoint *point)
{
    if (!point || !object.value(QStringLiteral("latitude")).isDouble()
        || !object.value(QStringLiteral("longitude")).isDouble())
        return false;
    point->latitude = object.value(QStringLiteral("latitude")).toDouble();
    point->longitude = object.value(QStringLiteral("longitude")).toDouble();
    return point->isValid();
}

bool readInteger(const QJsonObject &object, const QString &key, int *value)
{
    const QJsonValue jsonValue = object.value(key);
    if (!jsonValue.isDouble())
        return false;
    const double number = jsonValue.toDouble();
    const int integer = static_cast<int>(number);
    if (number != integer)
        return false;
    *value = integer;
    return true;
}

ChargerBusinessStatus parseStatus(const QString &status, bool *ok)
{
    *ok = true;
    if (status == QStringLiteral("idle")) return ChargerBusinessStatus::Idle;
    if (status == QStringLiteral("reserved")) return ChargerBusinessStatus::Reserved;
    if (status == QStringLiteral("charging")) return ChargerBusinessStatus::Charging;
    if (status == QStringLiteral("fault")) return ChargerBusinessStatus::Fault;
    if (status == QStringLiteral("restarting")) return ChargerBusinessStatus::Restarting;
    if (status == QStringLiteral("unknown")) return ChargerBusinessStatus::Unknown;
    *ok = false;
    return ChargerBusinessStatus::Unknown;
}

bool readBehavior(const QJsonObject &object,
                  const QString &name,
                  MapDemoBehavior *behavior,
                  QString *errorMessage)
{
    if (object.isEmpty())
        return true;
    const QString outcome = object.value(QStringLiteral("outcome"))
                                .toString(QStringLiteral("success"));
    if (outcome != QStringLiteral("success")
        && outcome != QStringLiteral("failure")
        && outcome != QStringLiteral("no-response"))
        return fail(errorMessage, QStringLiteral("%1 outcome is invalid.").arg(name));
    behavior->outcome = outcome;
    int value = 0;
    if (readInteger(object, QStringLiteral("responseDelayMs"), &value))
        behavior->responseDelayMs = qMax(0, value);
    if (readInteger(object, QStringLiteral("timeoutMs"), &value))
        behavior->timeoutMs = qMax(0, value);
    behavior->errorCode = object.value(QStringLiteral("errorCode")).toString();
    behavior->errorMessage = object.value(QStringLiteral("errorMessage")).toString();
    behavior->retryable = object.value(QStringLiteral("retryable")).toBool(true);
    return true;
}

} // namespace

bool loadMapDemoFixture(const QString &resourcePath,
                        MapDemoFixture *fixture,
                        QString *errorMessage)
{
    if (!fixture)
        return fail(errorMessage, QStringLiteral("Demo fixture output is null."));

    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly))
        return fail(errorMessage, QStringLiteral("Cannot open map Demo data: %1").arg(resourcePath));

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(errorMessage, QStringLiteral("Invalid map Demo JSON: %1").arg(parseError.errorString()));

    const QJsonObject mapDemo = document.object().value(QStringLiteral("mapDemo")).toObject();
    if (mapDemo.isEmpty())
        return fail(errorMessage, QStringLiteral("mapDemo object is required."));

    MapDemoFixture loaded;
    const QJsonObject scenario = mapDemo.value(QStringLiteral("scenario")).toObject();
    loaded.canvasState = scenario.value(QStringLiteral("canvasState"))
                             .toString(QStringLiteral("ready"));
    if (loaded.canvasState != QStringLiteral("ready")
        && loaded.canvasState != QStringLiteral("error")
        && loaded.canvasState != QStringLiteral("loading"))
        return fail(errorMessage, QStringLiteral("scenario.canvasState is invalid."));
    if (!readBehavior(scenario.value(QStringLiteral("location")).toObject(),
                      QStringLiteral("scenario.location"),
                      &loaded.locationBehavior, errorMessage)
        || !readBehavior(scenario.value(QStringLiteral("stations")).toObject(),
                         QStringLiteral("scenario.stations"),
                         &loaded.stationsBehavior, errorMessage))
        return false;
    const QJsonObject origin = mapDemo.value(QStringLiteral("origin")).toObject();
    if (!readPoint(origin, &loaded.location.point))
        return fail(errorMessage, QStringLiteral("mapDemo.origin must contain valid GCJ-02 coordinates."));
    const QJsonValue accuracy = origin.value(QStringLiteral("accuracyMeters"));
    if (accuracy.isDouble() && accuracy.toDouble() >= 0.0)
        loaded.location.accuracyMeters = accuracy.toDouble();
    loaded.location.capturedAtUtc = QDateTime::currentDateTimeUtc();
    loaded.location.source = LocationSource::Device;

    const QJsonValue stationsValue = mapDemo.value(QStringLiteral("stations"));
    if (!stationsValue.isArray())
        return fail(errorMessage, QStringLiteral("mapDemo.stations must be an array."));

    QSet<QString> stationIds;
    QSet<QString> chargerIds;
    int stationIndex = 0;
    for (const QJsonValue &stationValue : stationsValue.toArray()) {
        ++stationIndex;
        if (!stationValue.isObject())
            return fail(errorMessage, QStringLiteral("Station %1 must be an object.").arg(stationIndex));
        const QJsonObject object = stationValue.toObject();
        StationDetail detail;
        detail.stationId = object.value(QStringLiteral("stationId")).toString().trimmed();
        detail.summary.stationId = detail.stationId;
        detail.summary.name = object.value(QStringLiteral("name")).toString().trimmed();
        detail.summary.address = object.value(QStringLiteral("address")).toString().trimmed();
        if (detail.stationId.isEmpty() || detail.summary.name.isEmpty()
            || detail.summary.address.isEmpty() || stationIds.contains(detail.stationId))
            return fail(errorMessage, QStringLiteral("Station %1 has missing or duplicate identity data.").arg(stationIndex));
        stationIds.insert(detail.stationId);

        GeoPoint point;
        if (!readPoint(object, &point))
            return fail(errorMessage, QStringLiteral("Station %1 has invalid coordinates.").arg(stationIndex));
        detail.summary.point = point;

        int distanceMeters = 0;
        if (readInteger(object, QStringLiteral("distanceMeters"), &distanceMeters)
            && distanceMeters >= 0)
            detail.summary.distanceMeters = distanceMeters;
        int priceCents = 0;
        if (readInteger(object, QStringLiteral("priceCentsPerKwh"), &priceCents)
            && priceCents >= 0)
            detail.summary.priceCentsPerKwh = priceCents;

        const QJsonValue chargersValue = object.value(QStringLiteral("chargers"));
        if (!chargersValue.isArray())
            return fail(errorMessage, QStringLiteral("Station %1 chargers must be an array.").arg(stationIndex));
        for (const QJsonValue &chargerValue : chargersValue.toArray()) {
            if (!chargerValue.isObject())
                return fail(errorMessage, QStringLiteral("Station %1 contains an invalid charger.").arg(stationIndex));
            const QJsonObject chargerObject = chargerValue.toObject();
            ChargerSummary charger;
            charger.chargerId = chargerObject.value(QStringLiteral("chargerId")).toString().trimmed();
            charger.type = chargerObject.value(QStringLiteral("type")).toString().trimmed();
            if (charger.chargerId.isEmpty() || charger.type.isEmpty()
                || chargerIds.contains(charger.chargerId))
                return fail(errorMessage, QStringLiteral("Station %1 has missing or duplicate charger identity data.").arg(stationIndex));
            chargerIds.insert(charger.chargerId);
            const QJsonValue power = chargerObject.value(QStringLiteral("powerKw"));
            if (power.isDouble() && power.toDouble() >= 0.0)
                charger.powerKw = power.toDouble();
            bool statusOk = false;
            charger.businessStatus = parseStatus(
                chargerObject.value(QStringLiteral("status")).toString(), &statusOk);
            if (!statusOk)
                return fail(errorMessage, QStringLiteral("Charger %1 has an invalid status.").arg(charger.chargerId));
            charger.online = chargerObject.value(QStringLiteral("online")).toBool();
            charger.canStartCharging = chargerObject.value(QStringLiteral("canStartCharging")).toBool();
            charger.disabledReason = chargerObject.value(QStringLiteral("disabledReason")).toString();
            detail.chargers.append(charger);
        }

        detail.summary.totalCount = detail.chargers.size();
        for (const ChargerSummary &charger : detail.chargers) {
            if (charger.online && charger.businessStatus == ChargerBusinessStatus::Idle
                && charger.canStartCharging)
                ++detail.summary.availableCount;
        }
        detail.updatedAtUtc = QDateTime::currentDateTimeUtc();
        loaded.stations.append(detail);
    }

    *fixture = loaded;
    if (errorMessage)
        errorMessage->clear();
    return true;
}
