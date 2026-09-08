#include "massagehandler.h"
#include "network/backendclient.h"
#include "network/inetworktransport.h"
#include "network/qtnetworktransport.h"
#include "protocol.h"

#include <QHostAddress>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

namespace
{

class CountingTransport final : public INetworkTransport
{
    Q_OBJECT

public:
    explicit CountingTransport(QObject *parent = nullptr)
        : INetworkTransport(parent)
    {
    }

    void connectToServer() override
    {
        ++connectCount;
        connectedState = true;
        emit connected();
    }

    void disconnectFromServer() override
    {
        ++disconnectCount;
        if (connectedState) {
            connectedState = false;
            emit disconnected();
        }
    }

    bool send(const QByteArray &data) override
    {
        if (!connectedState) {
            return false;
        }
        sentData.append(data);
        return true;
    }

    bool isConnected() const override { return connectedState; }

    void simulateDisconnected()
    {
        connectedState = false;
        emit disconnected();
    }

    void simulateLateConnected()
    {
        connectedState = true;
        emit connected();
    }

    int connectCount = 0;
    int disconnectCount = 0;
    bool connectedState = false;
    QByteArray sentData;
};

class ControlledWriteSocket final : public QTcpSocket
{
    Q_OBJECT

public:
    void setBlocked(bool blocked) { m_blocked = blocked; }
    void resumeWrites()
    {
        m_blocked = false;
        emit bytesWritten(0);
    }

    QByteArray acceptedData;
    int writeCalls = 0;

protected:
    qint64 writeData(const char *data, qint64 size) override
    {
        ++writeCalls;
        if (m_blocked) {
            return 0;
        }
        const qint64 accepted = qMin<qint64>(3, size);
        acceptedData.append(data, static_cast<qsizetype>(accepted));
        return accepted;
    }

private:
    bool m_blocked = false;
};

} // namespace

class NetworkTransportTests final : public QObject
{
    Q_OBJECT

private slots:
    void transportConstructorDoesNotConnect();
    void backendStartConnectsAndParsesSplitAndStickyFrames();
    void backendHeartbeatStopsAfterDisconnect();
    void backendResetsPartialFrameBeforeReconnect();
    void transportPreservesQueuedFrameOrder();
    void transportContinuesAfterZeroAndPartialWrites();
    void disconnectDropsBlockedWriteQueue();
    void transportRejectsSendBeforeConnection();
    void transportReportsConnectionError();
    void startAndShutdownAreIdempotent();
    void shutdownStopsReconnectAndRejectsLateConnect();
};

void NetworkTransportTests::transportConstructorDoesNotConnect()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QSignalSpy connections(&server, &QTcpServer::newConnection);

    QtNetworkTransport transport(QStringLiteral("127.0.0.1"), server.serverPort());
    QTest::qWait(50);
    QCOMPARE(connections.count(), 0);

    transport.connectToServer();
    QTRY_COMPARE(connections.count(), 1);
}

void NetworkTransportTests::backendStartConnectsAndParsesSplitAndStickyFrames()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    QtNetworkTransport transport(QStringLiteral("127.0.0.1"), server.serverPort());
    BackendClient backend(&transport);
    QSignalSpy frames(&backend, &BackendClient::frameReceived);

    backend.start();
    QVERIFY(server.waitForNewConnection(1000));
    QTcpSocket *peer = server.nextPendingConnection();
    QVERIFY(peer);
    QTRY_COMPARE(backend.connectionState(), ConnectionState::Connected);

    QJsonObject firstPayload;
    firstPayload.insert(QStringLiteral("sequence"), 1);
    QJsonObject secondPayload;
    secondPayload.insert(QStringLiteral("sequence"), 2);
    const QByteArray first = MassageHandler::pack(HEARTBEAT_ACK, firstPayload);
    const QByteArray second = MassageHandler::pack(PHONE_LOGIN_ACK, secondPayload);

    // 先送不足一个帧头的半包，再把剩余数据与下一帧粘在一起。
    QCOMPARE(peer->write(first.left(5)), static_cast<qint64>(5));
    QTest::qWait(20);
    QCOMPARE(frames.count(), 0);

    const QByteArray tailAndNext = first.mid(5) + second;
    QCOMPARE(peer->write(tailAndNext), static_cast<qint64>(tailAndNext.size()));
    QTRY_COMPARE(frames.count(), 2);

    QCOMPARE(frames.at(0).at(0).toInt(), HEARTBEAT_ACK);
    QCOMPARE(frames.at(0).at(1).toJsonObject().value(QStringLiteral("sequence")).toInt(), 1);
    QCOMPARE(frames.at(1).at(0).toInt(), PHONE_LOGIN_ACK);
    QCOMPARE(frames.at(1).at(1).toJsonObject().value(QStringLiteral("sequence")).toInt(), 2);
}

void NetworkTransportTests::backendHeartbeatStopsAfterDisconnect()
{
    CountingTransport transport;
    BackendClient backend(&transport);
    backend.setHeartbeatIntervalMs(10);
    backend.start();

    QTRY_VERIFY(transport.sentData.size() >= FRAME_HEAD_LEN);
    QVERIFY(transport.sentData.startsWith(MassageHandler::makeHeartbeat()));

    transport.simulateDisconnected();
    const qsizetype bytesAtDisconnect = transport.sentData.size();
    QTest::qWait(40);
    QCOMPARE(transport.sentData.size(), bytesAtDisconnect);
}

void NetworkTransportTests::backendResetsPartialFrameBeforeReconnect()
{
    CountingTransport transport;
    BackendClient backend(&transport);
    backend.setReconnectIntervalMs(10);
    QSignalSpy frames(&backend, &BackendClient::frameReceived);
    backend.start();

    const QByteArray stale = MassageHandler::pack(DATA, QJsonObject{{QStringLiteral("old"), true}});
    emit transport.dataReceived(stale.left(7));
    transport.simulateDisconnected();
    QTRY_COMPARE(backend.connectionState(), ConnectionState::Connected);

    QJsonObject current;
    current.insert(QStringLiteral("current"), true);
    emit transport.dataReceived(MassageHandler::pack(DATA, current));

    QCOMPARE(frames.count(), 1);
    QCOMPARE(frames.at(0).at(0).toInt(), DATA);
    QCOMPARE(frames.at(0).at(1).toJsonObject(), current);
}

void NetworkTransportTests::transportPreservesQueuedFrameOrder()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    QtNetworkTransport transport(QStringLiteral("127.0.0.1"), server.serverPort());
    BackendClient backend(&transport);
    backend.start();
    QVERIFY(server.waitForNewConnection(1000));
    QTcpSocket *peer = server.nextPendingConnection();
    QVERIFY(peer);
    QTRY_COMPARE(backend.connectionState(), ConnectionState::Connected);

    QJsonObject payload;
    payload.insert(QStringLiteral("phone"), QStringLiteral("masked"));
    const QByteArray expected = MassageHandler::makeHeartbeat()
                              + MassageHandler::pack(PHONE_LOGIN_REQ, payload);

    QVERIFY(backend.sendFrame(HEARTBEAT));
    QVERIFY(backend.sendFrame(PHONE_LOGIN_REQ, payload));

    QByteArray received;
    QTRY_VERIFY(peer->bytesAvailable() >= expected.size());
    received.append(peer->readAll());
    QCOMPARE(received, expected);

    // 107 心跳必须是 12 字节帧头加零长度载荷，不能发送 JSON 文本 "{}"。
    QCOMPARE(MassageHandler::makeHeartbeat().size(), FRAME_HEAD_LEN);
}

void NetworkTransportTests::transportContinuesAfterZeroAndPartialWrites()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    auto *socket = new ControlledWriteSocket;
    socket->setBlocked(true);
    QtNetworkTransport transport(QStringLiteral("127.0.0.1"), server.serverPort(),
                                 socket, nullptr);
    transport.connectToServer();
    QVERIFY(server.waitForNewConnection(1000));
    QTRY_VERIFY(transport.isConnected());

    const QByteArray data("abcdefghij");
    QVERIFY(transport.send(data));
    QCOMPARE(socket->acceptedData.size(), 0);

    socket->resumeWrites();
    QTRY_COMPARE(socket->acceptedData, data);
    QVERIFY(socket->writeCalls >= 5); // 1 次返回 0，之后至少 4 次部分接受
}

void NetworkTransportTests::disconnectDropsBlockedWriteQueue()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    auto *socket = new ControlledWriteSocket;
    socket->setBlocked(true);
    QtNetworkTransport transport(QStringLiteral("127.0.0.1"), server.serverPort(),
                                 socket, nullptr);
    transport.connectToServer();
    QVERIFY(server.waitForNewConnection(1000));
    QTRY_VERIFY(transport.isConnected());

    QVERIFY(transport.send(QByteArrayLiteral("must-not-be-replayed")));
    transport.disconnectFromServer();
    socket->resumeWrites();
    QTest::qWait(20);

    QCOMPARE(socket->acceptedData.size(), 0);
    QVERIFY(!transport.isConnected());
}

void NetworkTransportTests::transportRejectsSendBeforeConnection()
{
    QtNetworkTransport transport(QStringLiteral("127.0.0.1"), 1);
    QVERIFY(!transport.send(MassageHandler::makeHeartbeat()));
}

void NetworkTransportTests::transportReportsConnectionError()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    const quint16 unusedPort = server.serverPort();
    server.close();

    QtNetworkTransport transport(QStringLiteral("127.0.0.1"), unusedPort);
    QSignalSpy errors(&transport, &INetworkTransport::transportError);
    transport.connectToServer();

    QTRY_VERIFY(!errors.isEmpty());
    QVERIFY(!transport.isConnected());
}

void NetworkTransportTests::startAndShutdownAreIdempotent()
{
    CountingTransport transport;
    BackendClient backend(&transport);

    backend.start();
    backend.start();
    QCOMPARE(transport.connectCount, 1);

    backend.shutdown();
    backend.shutdown();
    QCOMPARE(transport.disconnectCount, 1);
    QCOMPARE(backend.connectionState(), ConnectionState::Disconnected);
}

void NetworkTransportTests::shutdownStopsReconnectAndRejectsLateConnect()
{
    CountingTransport transport;
    BackendClient backend(&transport);
    backend.setReconnectIntervalMs(20);

    backend.start();
    QCOMPARE(transport.connectCount, 1);
    QCOMPARE(backend.connectionState(), ConnectionState::Connected);

    transport.simulateDisconnected();
    QCOMPARE(backend.connectionState(), ConnectionState::Reconnecting);
    backend.shutdown();
    QCOMPARE(backend.connectionState(), ConnectionState::Disconnected);

    QTest::qWait(60);
    QCOMPARE(transport.connectCount, 1);

    transport.simulateLateConnected();
    QCOMPARE(backend.connectionState(), ConnectionState::Disconnected);
    QVERIFY(!transport.connectedState);
    QCOMPARE(transport.disconnectCount, 2);
}

QTEST_MAIN(NetworkTransportTests)

#include "network-transport-tests.moc"
