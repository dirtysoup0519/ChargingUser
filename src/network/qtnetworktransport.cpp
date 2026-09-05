#include "qtnetworktransport.h"

#include <QTcpSocket>

QtNetworkTransport::QtNetworkTransport(const QString &host, quint16 port, QObject *parent)
    : INetworkTransport(parent)
    , m_host(host)
    , m_port(port)
    , m_socket(new QTcpSocket(this))
{
    connect(m_socket, &QTcpSocket::connected,
            this, &QtNetworkTransport::connected);
    connect(m_socket, &QTcpSocket::disconnected,
            this, &QtNetworkTransport::disconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, [this] {
        emit dataReceived(m_socket->readAll());
    });

    using SocketError = QAbstractSocket::SocketError;
    connect(m_socket, &QTcpSocket::errorOccurred, this,
            [this](SocketError error) {
        // 连接被拒绝等错误在未建立连接时也会上报，交由上层按状态处理
        Q_UNUSED(error);
        emit transportError(m_socket->errorString());
    });

    m_socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    m_socket->connectToHost(m_host, m_port);
}

QtNetworkTransport::~QtNetworkTransport() = default;

void QtNetworkTransport::connectToServer()
{
    if (m_socket->state() == QAbstractSocket::UnconnectedState) {
        m_socket->connectToHost(m_host, m_port);
    }
}

void QtNetworkTransport::disconnectFromServer()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
    }
}

bool QtNetworkTransport::send(const QByteArray &data)
{
    if (m_socket->state() != QAbstractSocket::ConnectedState) {
        return false;
    }
    return m_socket->write(data) == data.size();
}

bool QtNetworkTransport::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}
