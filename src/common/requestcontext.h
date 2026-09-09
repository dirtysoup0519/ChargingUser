#pragma once

#include <QMetaType>
#include <QString>

struct RequestContext
{
    QString requestId;
    QString operationId;

    bool isValid() const
    {
        return !requestId.isEmpty();
    }

    bool isMutation() const
    {
        return !operationId.isEmpty();
    }
};

Q_DECLARE_METATYPE(RequestContext)
