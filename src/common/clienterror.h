#pragma once

#include <QMetaType>
#include <QString>

struct ClientError
{
    QString requestId;
    QString operationId;
    QString code;
    QString displayMessage;
    bool retryable = false;
    bool resultUnknown = false;
};

Q_DECLARE_METATYPE(ClientError)
