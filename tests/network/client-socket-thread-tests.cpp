#include "network/clientsocketthreadmanager.h"
#include "massagehandler.h"
#include "protocol.h"

#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QtTest>

class ClientSocketThreadTests final : public QObject
{
    Q_OBJECT

private slots:
    void startsAndStopsWorkerThread();
    void rejectsSendBeforeStart();
    void sendsAndReceivesFramesAcrossWorkerThread();
};

void ClientSocketThreadTests::startsAndStopsWorkerThread()
{
    ClientSocketThreadManager manager(QStringLiteral("127.0.0.1"), 12345);
    QSignalSpy started(&manager,
                       &ClientSocketThreadManager::networkThreadStarted);
    QSignalSpy stopped(&manager,
                       &ClientSocketThreadManager::networkThreadStopped);

    manager.start();
    QTRY_COMPARE(started.count(), 1);
    QVERIFY(manager.isNetworkThreadRunning());

    // 重复启动不得创建第二条网络线程。
    manager.start();
    QTest::qWait(20);
    QCOMPARE(started.count(), 1);

    manager.stop();
    QTRY_COMPARE(stopped.count(), 1);
    QVERIFY(!manager.isNetworkThreadRunning());
}

void ClientSocketThreadTests::rejectsSendBeforeStart()
{
    ClientSocketThreadManager manager(QStringLiteral("127.0.0.1"), 12345);
    QSignalSpy failures(&manager, &IBackendChannel::frameSendFailed);

    manager.sendFrame(107);

    QCOMPARE(failures.count(), 1);
    QCOMPARE(failures.first().at(0).toInt(), 107);
}

void ClientSocketThreadTests::sendsAndReceivesFramesAcrossWorkerThread()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    ClientSocketThreadManager manager(QStringLiteral("127.0.0.1"),
                                      server.serverPort());
    QSignalSpy states(&manager, &IBackendChannel::connectionStateChanged);
    QSignalSpy frames(&manager, &IBackendChannel::frameReceived);
    QSignalSpy failures(&manager, &IBackendChannel::frameSendFailed);
    bool deliveredOnManagerThread = false;
    connect(&manager, &IBackendChannel::frameReceived, &manager,
            [&manager, &deliveredOnManagerThread] {
        deliveredOnManagerThread = QThread::currentThread() == manager.thread();
    });

    manager.start();
    QVERIFY(server.waitForNewConnection(1000));
    QTcpSocket *peer = server.nextPendingConnection();
    QVERIFY(peer);
    QTRY_COMPARE(manager.connectionState(), ConnectionState::Connected);
    QVERIFY(!states.isEmpty());

    const QJsonObject request{{QStringLiteral("sequence"), 7}};
    manager.sendFrame(PHONE_LOGIN_REQ, request);

    const QByteArray expected = MassageHandler::pack(PHONE_LOGIN_REQ, request);
    QTRY_VERIFY(peer->bytesAvailable() >= expected.size());
    QCOMPARE(peer->readAll(), expected);
    QCOMPARE(failures.count(), 0);

    const QJsonObject firstPayload{{QStringLiteral("sequence"), 8}};
    const QJsonObject secondPayload{{QStringLiteral("sequence"), 9}};
    const QByteArray first = MassageHandler::pack(PHONE_LOGIN_ACK, firstPayload);
    const QByteArray second = MassageHandler::pack(HEARTBEAT_ACK, secondPayload);

    // 半包和粘包都在网络线程解析，只把完整 JSON 对象送回主线程。
    QCOMPARE(peer->write(first.left(5)), static_cast<qint64>(5));
    QTest::qWait(20);
    QCOMPARE(frames.count(), 0);
    QCOMPARE(peer->write(first.mid(5) + second),
             static_cast<qint64>(first.size() - 5 + second.size()));

    QTRY_COMPARE(frames.count(), 2);
    QCOMPARE(frames.at(0).at(0).toInt(), PHONE_LOGIN_ACK);
    QCOMPARE(frames.at(0).at(1).toJsonObject(), firstPayload);
    QCOMPARE(frames.at(1).at(0).toInt(), HEARTBEAT_ACK);
    QCOMPARE(frames.at(1).at(1).toJsonObject(), secondPayload);
    QVERIFY(deliveredOnManagerThread);

    manager.stop();
    QTRY_VERIFY(!manager.isNetworkThreadRunning());
}

QTEST_MAIN(ClientSocketThreadTests)

#include "client-socket-thread-tests.moc"
