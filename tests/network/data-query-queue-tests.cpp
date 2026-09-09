/* BackendClient GETDATA 队列行为测试：
 *  - 队首查询无应答时，队列超时后自动跳过并继续发送后续查询
 *  - cancelQuery 能移除排队中的查询，或摘除活动中的查询并立即推进
 *  - 收到应答后超时定时器停止，不误报 networkError
 */
#include "massagehandler.h"
#include "network/backendclient.h"
#include "network/inetworktransport.h"
#include "protocol.h"

#include <QJsonArray>
#include <QSignalSpy>

#include <QtTest>

namespace {

class FakeTransport final : public INetworkTransport
{
    Q_OBJECT

public:
    explicit FakeTransport(QObject *parent = nullptr) : INetworkTransport(parent) {}

    void connectToServer() override
    {
        if (!m_connected) {
            m_connected = true;
            emit connected();
        }
    }
    void disconnectFromServer() override
    {
        if (m_connected) {
            m_connected = false;
            emit disconnected();
        }
    }
    bool isConnected() const override { return m_connected; }
    bool send(const QByteArray &data) override
    {
        if (!m_connected) return false;
        m_sentFrames.append(data);
        return true;
    }
    void simulateIncoming(const QByteArray &frame) { emit dataReceived(frame); }
    void clearSentFrames() { m_sentFrames.clear(); }

    QList<QByteArray> m_sentFrames;

private:
    bool m_connected = false;
};

QList<QPair<int, QJsonObject>> decodeFrames(const QList<QByteArray> &bytesList)
{
    MassageHandler decoder;
    QList<QPair<int, QJsonObject>> frames;
    QObject::connect(&decoder, &MassageHandler::frameReady,
                     [&frames](int msgType, const QByteArray &payload) {
        frames.append(qMakePair(msgType, MassageHandler::fromPayload(payload)));
    });
    for (const QByteArray &bytes : bytesList) {
        decoder.feed(bytes);
    }
    return frames;
}

/* 连接建立时自动发出的 107 心跳属于生命周期帧，业务断言只关心 GETDATA。 */
QList<QPair<int, QJsonObject>> dataQueries(const QList<QByteArray> &bytesList)
{
    auto frames = decodeFrames(bytesList);
    frames.erase(std::remove_if(frames.begin(), frames.end(),
                                [](const QPair<int, QJsonObject> &frame) {
                                    return frame.first != GETDATA;
                                }),
                 frames.end());
    return frames;
}

QJsonObject makeQuery(const QString &requestId, const QString &table)
{
    return QJsonObject{{QStringLiteral("table"), table},
                       {QStringLiteral("cond"), QJsonObject{}},
                       {QStringLiteral("requestId"), requestId}};
}

void feed(FakeTransport *transport, int type, const QJsonObject &payload)
{
    transport->simulateIncoming(MassageHandler::pack(type, payload));
}

} // namespace

class DataQueryQueueTests final : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void timeoutSkipsStuckQueryAndContinues();
    void responseStopsTimeoutTimer();
    void cancelQueryRemovesQueuedRequest();
    void cancelQueryDropsActiveRequestAndAdvances();

private:
    FakeTransport *m_transport = nullptr;
    BackendClient *m_backend = nullptr;
};

void DataQueryQueueTests::init()
{
    m_transport = new FakeTransport(this);
    m_backend = new BackendClient(m_transport, this);
    m_backend->setDataQueryTimeoutMs(60);
    m_backend->start();
    m_transport->clearSentFrames();
}

void DataQueryQueueTests::timeoutSkipsStuckQueryAndContinues()
{
    QSignalSpy errors(m_backend, &BackendClient::networkError);
    QVERIFY(m_backend->sendFrame(GETDATA, makeQuery(QStringLiteral("req-a"), QStringLiteral("user"))));
    // req-b 排队在永不应答的 req-a 之后
    QVERIFY(m_backend->sendFrame(GETDATA, makeQuery(QStringLiteral("req-b"), QStringLiteral("reservation"))));
    QCOMPARE(dataQueries(m_transport->m_sentFrames).size(), 1);

    // 队首超时被跳过，req-b 得以发送，队列不再永久阻塞
    QTRY_COMPARE_WITH_TIMEOUT(dataQueries(m_transport->m_sentFrames).size(), 2, 2000);
    QCOMPARE(errors.count(), 1);
    const auto frames = dataQueries(m_transport->m_sentFrames);
    QCOMPARE(frames.last().second.value(QStringLiteral("requestId")).toString(),
             QStringLiteral("req-b"));
}

void DataQueryQueueTests::responseStopsTimeoutTimer()
{
    QSignalSpy errors(m_backend, &BackendClient::networkError);
    QVERIFY(m_backend->sendFrame(GETDATA, makeQuery(QStringLiteral("req-a"), QStringLiteral("user"))));
    feed(m_transport, DATA,
         QJsonObject{{QStringLiteral("data"), QJsonArray{}}});
    QTest::qWait(150);
    QCOMPARE(errors.count(), 0);
}

void DataQueryQueueTests::cancelQueryRemovesQueuedRequest()
{
    QVERIFY(m_backend->sendFrame(GETDATA, makeQuery(QStringLiteral("req-a"), QStringLiteral("user"))));
    QVERIFY(m_backend->sendFrame(GETDATA, makeQuery(QStringLiteral("req-b"), QStringLiteral("reservation"))));
    // req-b 尚未发送：取消后队列中不再有它
    QVERIFY(m_backend->cancelQuery(QStringLiteral("req-b")));
    feed(m_transport, DATA,
         QJsonObject{{QStringLiteral("data"), QJsonArray{}}});
    QCOMPARE(dataQueries(m_transport->m_sentFrames).size(), 1);
    // 取消不存在的 requestId 不影响队列
    QVERIFY(!m_backend->cancelQuery(QStringLiteral("req-missing")));
}

void DataQueryQueueTests::cancelQueryDropsActiveRequestAndAdvances()
{
    QVERIFY(m_backend->sendFrame(GETDATA, makeQuery(QStringLiteral("req-a"), QStringLiteral("user"))));
    QVERIFY(m_backend->sendFrame(GETDATA, makeQuery(QStringLiteral("req-b"), QStringLiteral("reservation"))));
    QCOMPARE(dataQueries(m_transport->m_sentFrames).size(), 1);
    // 摘除活动中的 req-a 后，req-b 立即发出
    QVERIFY(m_backend->cancelQuery(QStringLiteral("req-a")));
    QCOMPARE(dataQueries(m_transport->m_sentFrames).size(), 2);
    const auto frames = dataQueries(m_transport->m_sentFrames);
    QCOMPARE(frames.last().second.value(QStringLiteral("requestId")).toString(),
             QStringLiteral("req-b"));
    // req-a 的迟到应答按队列归属注入 req-b 的关联字段（不回声服务端的既有限制）
    feed(m_transport, DATA, QJsonObject{{QStringLiteral("data"), QJsonArray{}}});
}

QTEST_GUILESS_MAIN(DataQueryQueueTests)
#include "data-query-queue-tests.moc"
