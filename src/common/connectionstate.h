#pragma once

#include <QMetaType>

enum class ConnectionState
{
    Disconnected,
    Connecting,
    Connected,
    Reconnecting
};

Q_DECLARE_METATYPE(ConnectionState)
