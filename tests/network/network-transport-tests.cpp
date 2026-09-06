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

} // namespace

class NetworkTransportTests final : public QObject
{
    Q_OBJECT

private slots:
    void transportConstructorDoesNotConnect();
    void backendStartConnectsAndParsesSplitAndStickyFrames();
    void transportPreservesQueuedFrameOrder();
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
