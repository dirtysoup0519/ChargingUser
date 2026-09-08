#include "demo/chargedemofixtureloader.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

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
