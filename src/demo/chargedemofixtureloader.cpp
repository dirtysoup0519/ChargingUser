#include "demo/chargedemofixtureloader.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

bool loadChargeConfirmationDemo(const QString &resourcePath,
                                ChargeConfirmationViewState *state,
                                QString *errorMessage)
{
    if (!state)
        return false;
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("无法打开充电确认 Demo 数据");
        return false;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    const QJsonObject object = document.object().value(
        QStringLiteral("chargeConfirmationDemo")).toObject();
    if (error.error != QJsonParseError::NoError || object.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("充电确认 Demo 数据格式错误");
        return false;
    }
    state->walletBalanceText = object.value(QStringLiteral("walletBalanceText")).toString();
    state->canRecharge = object.value(QStringLiteral("canRecharge")).toBool(true);
    return true;
}

bool loadChargingSessionDemo(const QString &resourcePath,
                             QList<ChargingSessionViewState> *sessions,
                             QString *errorMessage)
{
    if (!sessions) return false;
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("无法打开充电进行 Demo 数据");
        return false;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    const QJsonArray array = document.object().value(QStringLiteral("chargingSessions")).toArray();
    if (error.error != QJsonParseError::NoError || array.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("充电进行 Demo 数据格式错误");
        return false;
    }
    sessions->clear();
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        ChargingSessionViewState state;
        state.status = ChargingSessionStatus::Charging;
        state.currentPowerText = object.value(QStringLiteral("currentPowerText")).toString();
        state.energyText = object.value(QStringLiteral("energyText")).toString();
        state.durationText = object.value(QStringLiteral("durationText")).toString();
        state.amountText = object.value(QStringLiteral("amountText")).toString();
        state.progressPercent = object.value(QStringLiteral("progressPercent")).toInt(-1);
        state.canStop = true;
        sessions->append(state);
    }
    return !sessions->isEmpty();
}
