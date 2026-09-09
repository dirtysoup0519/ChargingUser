#include "demo/reservationdemofixtureloader.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

bool loadReservationDemoFixture(const QString &resourcePath,
                                ReservationDemoFixture *fixture,
                                QString *errorMessage)
{
    if (!fixture) return false;
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("无法打开预约 Demo 数据");
        return false;
    }
    QJsonParseError parseError;
    const QJsonObject object = QJsonDocument::fromJson(file.readAll(), &parseError)
                                   .object().value(QStringLiteral("reservationDemo")).toObject();
    if (parseError.error != QJsonParseError::NoError || object.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("预约 Demo 数据格式错误");
        return false;
    }
    fixture->depositText = object.value(QStringLiteral("depositText")).toString();
    fixture->durationText = object.value(QStringLiteral("durationText")).toString();
    fixture->depositPolicyText = object.value(QStringLiteral("depositPolicyText"))
                                     .toString(QStringLiteral("超过预约时限仍未开始充电，将按规则扣除预约押金"));
    fixture->durationSeconds = qMax(1, object.value(QStringLiteral("durationSeconds")).toInt(900));
    fixture->responseDelayMs = qMax(0, object.value(QStringLiteral("responseDelayMs")).toInt(450));
    fixture->cancellationCooldownSeconds = qMax(
        0, object.value(QStringLiteral("cancellationCooldownSeconds")).toInt(3));
    const QJsonObject cancellation = object.value(QStringLiteral("cancellation")).toObject();
    fixture->cancellationResponseDelayMs = qMax(
        0, cancellation.value(QStringLiteral("responseDelayMs")).toInt(450));
    fixture->cancellationOutcome = cancellation.value(QStringLiteral("outcome"))
                                       .toString(QStringLiteral("success"));
    fixture->cancellationRetryOutcome = cancellation.value(QStringLiteral("retryOutcome"))
                                            .toString(QStringLiteral("success"));
    fixture->cancellationFailureMessage = cancellation.value(QStringLiteral("failureMessage"))
                                              .toString(QStringLiteral("取消预约失败，请重试"));
    fixture->cancellationUnknownMessage = cancellation.value(QStringLiteral("resultUnknownMessage"))
                                              .toString(QStringLiteral("取消结果正在确认，请勿重复操作"));
    fixture->outcome = object.value(QStringLiteral("outcome")).toString(QStringLiteral("success"));
    return true;
}
