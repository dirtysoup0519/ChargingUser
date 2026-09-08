#include "network/clientsocketthreadmanager.h"

#include <QSignalSpy>
#include <QtTest>

class ClientSocketThreadTests final : public QObject
{
    Q_OBJECT

private slots:
    void startsAndStopsWorkerThread();
    void rejectsSendBeforeStart();
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

QTEST_MAIN(ClientSocketThreadTests)

#include "client-socket-thread-tests.moc"

