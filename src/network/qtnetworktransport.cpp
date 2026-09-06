#include "qtnetworktransport.h"

#include <QTcpSocket>

QtNetworkTransport::QtNetworkTransport(const QString &host, quint16 port, QObject *parent)
    : INetworkTransport(parent)
    , m_host(host)
    , m_port(port)
    , m_socket(new QTcpSocket(this))
{
    connect(m_socket, &QTcpSocket::connected, this, [this] {
        flushWriteQueue();
        emit connected();
    });
    connect(m_socket, &QTcpSocket::disconnected, this, [this] {
        // 旧连接上尚未写出的字节不能在下一条连接上自动重放。
        clearWriteQueue();
        emit disconnected();
    });
    connect(m_socket, &QTcpSocket::readyRead, this, [this] {
        emit dataReceived(m_socket->readAll());
    });
    connect(m_socket, &QTcpSocket::bytesWritten, this, [this](qint64 bytes) {
        Q_UNUSED(bytes);
        // write() 可能只接收部分数据；已写出后继续泵送剩余队列。
        flushWriteQueue();
    });

    using SocketError = QAbstractSocket::SocketError;
    connect(m_socket, &QTcpSocket::errorOccurred, this,
            [this](SocketError error) {
        // 连接被拒绝等错误在未建立连接时也会上报，交由上层按状态处理
        Q_UNUSED(error);
        clearWriteQueue();
        emit transportError(m_socket->errorString());
    });

    m_socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
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
    clearWriteQueue();
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        // shutdown 不应继续发送 QTcpSocket 内部尚未刷出的旧请求。
        m_socket->abort();
    }
}

bool QtNetworkTransport::send(const QByteArray &data)
{
    if (m_socket->state() != QAbstractSocket::ConnectedState) {
        return false;
    }

    if (data.isEmpty()) {
        return true;
    }

    m_pendingWrite.append(data);
    return flushWriteQueue();
}

bool QtNetworkTransport::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

bool QtNetworkTransport::flushWriteQueue()
{
    if (m_socket->state() != QAbstractSocket::ConnectedState) {
        return m_pendingWrite.isEmpty();
    }

    while (!m_pendingWrite.isEmpty()) {
        const qint64 accepted = m_socket->write(m_pendingWrite);
        if (accepted < 0) {
            clearWriteQueue();
            return false;
        }
        if (accepted == 0) {
            // 等待 socket 的 bytesWritten 信号后继续，数据仍安全留在队列中。
            return true;
        }
        m_pendingWrite.remove(0, static_cast<qsizetype>(accepted));
    }
    return true;
}

void QtNetworkTransport::clearWriteQueue()
{
    m_pendingWrite.clear();
}
