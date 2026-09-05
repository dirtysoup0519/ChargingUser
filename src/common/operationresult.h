#pragma once

#include <QMetaType>
#include <QString>

struct OperationResult
{
    QString requestId;
    QString operationId;
};

Q_DECLARE_METATYPE(OperationResult)
