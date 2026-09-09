#pragma once

#include <QString>

struct ReservationDemoFixture
{
    QString depositText;
    QString durationText;
    QString depositPolicyText;
    int durationSeconds = 900;
    int responseDelayMs = 450;
    int cancellationCooldownSeconds = 3;
    int cancellationResponseDelayMs = 450;
    QString cancellationOutcome = QStringLiteral("success");
    QString cancellationRetryOutcome = QStringLiteral("success");
    QString cancellationFailureMessage;
    QString cancellationUnknownMessage;
    QString outcome = QStringLiteral("success");
};

bool loadReservationDemoFixture(const QString &resourcePath,
                                ReservationDemoFixture *fixture,
                                QString *errorMessage = nullptr);
