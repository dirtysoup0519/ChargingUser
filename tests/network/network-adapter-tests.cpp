/* RealUserNetworkApi 适配器测试
 * 用 MockTransport 模拟 socket：直接注入协议帧、控制连接状态。
 * 覆盖合同 §11 v1.1 的适配器语义：
 *  - 请求载荷携带 requestId；117/118 走新消息码
 *  - 应答优先按回显 requestId 匹配，无回显时按类型回退
 *  - 查询超时可重试；改昵称超时 = 结果未知
 *  - 3xx 映射业务错误码；断线使在途请求失败
 *  - 退出登录尽力发送，不等待应答
 */
#include "massagehandler.h"
#include "network/backendclient.h"
#include "network/inetworktransport.h"
#include "network/realusernetworkapi.h"
#include "protocol.h"

#include <QSignalSpy>
#include <QtTest>

namespace
{

/* 模拟传输层：记录发送字节、按需注入接收数据 */
class MockTransport final : public INetworkTransport
{
    Q_OBJECT

public:
    explicit MockTransport(QObject *parent = nullptr)
        : INetworkTransport(parent)
    {
    }

    void connectToServer() override
    {
        if (m_failConnect) {
            m_connected = false;
            // 模拟连接被拒：只发错误，不发 connected/disconnected
            emit transportError(QStringLiteral("connection refused"));
            return;
        }
        m_connected = true;
        emit connected();
    }

    void disconnectFromServer() override
    {
        if (m_connected) {
            m_connected = false;
            emit disconnected();
        }
    }

    bool send(const QByteArray &data) override
    {
        if (m_failSend || !m_connected) {
            return false;
        }
        m_sentFrames.append(data);
        return true;
    }

    bool isConnected() const override { return m_connected; }

    void simulateIncoming(const QByteArray &frame) { emit dataReceived(frame); }
    void simulateDisconnected()
    {
        m_connected = false;
        emit disconnected();
    }
    void setFailSend(bool failSend) { m_failSend = failSend; }
    void setFailConnect(bool failConnect) { m_failConnect = failConnect; }

    QList<QByteArray> m_sentFrames;

private:
    bool m_connected = false;
    bool m_failSend = false;
    bool m_failConnect = false;
};

/* 解码适配器发出的协议帧（复用共享编解码器） */
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

RequestContext makeContext(const QString &requestId)
{
    RequestContext context;
    context.requestId = requestId;
    context.operationId = requestId + QLatin1String("-op");
    return context;
}

} // namespace

class NetworkAdapterTests final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void loginSendsPhoneRequestWithRequestId();
    void loginAckMapsToLoginResult();
    void loginAckWithoutEchoStillMatches();
    void serverErrorMapsToClientError();
    void queryTimeoutIsRetryable();
    void nicknameTimeoutIsResultUnknown();
    void lateResponseAfterTimeoutIgnored();
    void disconnectFailsPendingRequests();
    void logoutSendsImmediatelyWithoutAck();
    void sendFailureReportsRequestFailed();
    void reconnectsAfterInitialConnectFailure();
    void malformedLoginResponseDoesNotPolluteSession();
    void malformedNicknameResponseIsResultUnknown();
    void loginWhenNotConnectedIsRetryable();
};

void NetworkAdapterTests::initTestCase()
{
    qRegisterMetaType<ClientError>();
    qRegisterMetaType<LoginResult>();
    qRegisterMetaType<UserProfileResult>();
    qRegisterMetaType<OperationResult>();
}

void NetworkAdapterTests::loginSendsPhoneRequestWithRequestId()
{
    MockTransport transport;
    BackendClient backend(&transport);
    backend.start();
    RealUserNetworkApi api(&backend);

    api.loginByPhone(QStringLiteral("13800138000"), makeContext(QStringLiteral("req-x")));
    const auto frames = decodeFrames(transport.m_sentFrames);

    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.first().first, PHONE_LOGIN_REQ);
    const QJsonObject payload = frames.first().second;
    QCOMPARE(payload.value(QLatin1String("phone")).toString(),
             QStringLiteral("13800138000"));
    QCOMPARE(payload.value(QLatin1String("requestId")).toString(),
             QStringLiteral("req-x"));
}

void NetworkAdapterTests::loginAckMapsToLoginResult()
{
    MockTransport transport;
    BackendClient backend(&transport);
    backend.start();
    RealUserNetworkApi api(&backend);
    api.setRequestTimeoutMs(5000);

    QSignalSpy successes(&api, &IUserNetworkApi::loginSucceeded);
    api.loginByPhone(QStringLiteral("13800138000"), makeContext(QStringLiteral("req-1")));

    QJsonObject ack;
    ack.insert(QStringLiteral("username"), QStringLiteral("U13800138000"));
    ack.insert(QStringLiteral("phone"), QStringLiteral("13800138000"));
    ack.insert(QStringLiteral("nickname"), QStringLiteral("用户0000"));
    ack.insert(QStringLiteral("status"), QStringLiteral("Normal"));
    ack.insert(QStringLiteral("balanceCents"), 0);
    ack.insert(QStringLiteral("autoRegistered"), true);
    ack.insert(QStringLiteral("requestId"), QStringLiteral("req-1"));
    transport.simulateIncoming(MassageHandler::pack(PHONE_LOGIN_ACK, ack));

    QCOMPARE(successes.count(), 1);
    const LoginResult result =
        qvariant_cast<LoginResult>(successes.takeFirst().at(0));
    QCOMPARE(result.requestId, QStringLiteral("req-1"));
    QVERIFY(result.isNewUser);
    QCOMPARE(result.session.profile.userId, QStringLiteral("U13800138000"));
    QCOMPARE(result.session.profile.nickname, QStringLiteral("用户0000"));
    QCOMPARE(result.session.accountStatus, AccountStatus::Normal);
    QVERIFY(result.session.authenticated);
}

void NetworkAdapterTests::loginAckWithoutEchoStillMatches()
{
    MockTransport transport;
    BackendClient backend(&transport);
    backend.start();
    RealUserNetworkApi api(&backend);
    api.setRequestTimeoutMs(5000);

    QSignalSpy successes(&api, &IUserNetworkApi::loginSucceeded);
    api.loginByPhone(QStringLiteral("13800138000"), makeContext(QStringLiteral("req-2")));

    // 服务端未回显 requestId（v1.1 允许）→ 按应答类型回退匹配
    QJsonObject ack;
    ack.insert(QStringLiteral("username"), QStringLiteral("U13800138000"));
    ack.insert(QStringLiteral("status"), QStringLiteral("Frozen"));
    transport.simulateIncoming(MassageHandler::pack(PHONE_LOGIN_ACK, ack));

    QCOMPARE(successes.count(), 1);
    const LoginResult result =
        qvariant_cast<LoginResult>(successes.takeFirst().at(0));
    QCOMPARE(result.session.accountStatus, AccountStatus::Frozen);
}

void NetworkAdapterTests::serverErrorMapsToClientError()
{
    MockTransport transport;
    BackendClient backend(&transport);
    backend.start();
    RealUserNetworkApi api(&backend);
    api.setRequestTimeoutMs(5000);

    QSignalSpy failures(&api, &IUserNetworkApi::requestFailed);
    api.loginByPhone(QStringLiteral("13800138000"), makeContext(QStringLiteral("req-3")));

    QJsonObject err;
    err.insert(QStringLiteral("code"), BIZ_ERR_PARAM);
    err.insert(QStringLiteral("reason"), QStringLiteral("bad phone"));
    transport.simulateIncoming(MassageHandler::pack(PARAM_ERROR, err));

    QCOMPARE(failures.count(), 1);
    const ClientError error =
        qvariant_cast<ClientError>(failures.takeFirst().at(0));
    QCOMPARE(error.code, QStringLiteral(BIZ_ERR_PARAM));
    QCOMPARE(error.displayMessage, QStringLiteral("bad phone"));
    QVERIFY(!error.retryable);
    QCOMPARE(error.requestId, QStringLiteral("req-3"));
}

void NetworkAdapterTests::queryTimeoutIsRetryable()
{
    MockTransport transport;
    BackendClient backend(&transport);
    backend.start();
    RealUserNetworkApi api(&backend);
    api.setRequestTimeoutMs(30);

    QSignalSpy failures(&api, &IUserNetworkApi::requestFailed);
    api.queryCurrentUser(QStringLiteral("U13800138000"),
                         makeContext(QStringLiteral("req-4")));
    QTest::qWait(80);

    QCOMPARE(failures.count(), 1);
    const ClientError error =
        qvariant_cast<ClientError>(failures.takeFirst().at(0));
    QCOMPARE(error.code, QStringLiteral("request-timeout"));
    QVERIFY(error.retryable);
    QVERIFY(!error.resultUnknown);
}

void NetworkAdapterTests::nicknameTimeoutIsResultUnknown()
{
    MockTransport transport;
    BackendClient backend(&transport);
    backend.start();
    RealUserNetworkApi api(&backend);
    api.setRequestTimeoutMs(30);

    QSignalSpy failures(&api, &IUserNetworkApi::requestFailed);
    api.updateNickname(QStringLiteral("U13800138000"), QStringLiteral("老王"),
                       makeContext(QStringLiteral("req-5")));
    QTest::qWait(80);

    QCOMPARE(failures.count(), 1);
    const ClientError error =
        qvariant_cast<ClientError>(failures.takeFirst().at(0));
    QCOMPARE(error.code, QStringLiteral("result-unknown"));
    QCOMPARE(error.operationId, QStringLiteral("req-5-op"));
    QVERIFY(!error.retryable);
    QVERIFY(error.resultUnknown);
}

void NetworkAdapterTests::lateResponseAfterTimeoutIgnored()
{
    MockTransport transport;
    BackendClient backend(&transport);
    backend.start();
    RealUserNetworkApi api(&backend);
    api.setRequestTimeoutMs(30);

    QSignalSpy successes(&api, &IUserNetworkApi::nicknameUpdateSucceeded);
    QSignalSpy failures(&api, &IUserNetworkApi::requestFailed);
    api.updateNickname(QStringLiteral("U13800138000"), QStringLiteral("老王"),
                       makeContext(QStringLiteral("req-6")));
    QTest::qWait(80);
    QCOMPARE(failures.count(), 1);

    // 超时已按结果未知处理，此后到达的 219 必须被丢弃
    QJsonObject ack;
    ack.insert(QStringLiteral("ok"), true);
    ack.insert(QStringLiteral("nickname"), QStringLiteral("老王"));
    ack.insert(QStringLiteral("requestId"), QStringLiteral("req-6"));
    transport.simulateIncoming(MassageHandler::pack(UPDNICK_ACK, ack));

    QCOMPARE(successes.count(), 0);
}

void NetworkAdapterTests::disconnectFailsPendingRequests()
{
    MockTransport transport;
    BackendClient backend(&transport);
    backend.start();
    RealUserNetworkApi api(&backend);
    api.setRequestTimeoutMs(5000);

    QSignalSpy failures(&api, &IUserNetworkApi::requestFailed);
    api.loginByPhone(QStringLiteral("13800138000"), makeContext(QStringLiteral("req-7")));
    transport.simulateDisconnected();

    QCOMPARE(failures.count(), 1);
    const ClientError error =
        qvariant_cast<ClientError>(failures.takeFirst().at(0));
    QCOMPARE(error.code, QStringLiteral("connection-lost"));
    QVERIFY(error.retryable);
}

void NetworkAdapterTests::logoutSendsImmediatelyWithoutAck()
{
    MockTransport transport;
    BackendClient backend(&transport);
    backend.start();
    RealUserNetworkApi api(&backend);

    QSignalSpy successes(&api, &IUserNetworkApi::logoutSucceeded);
    QSignalSpy failures(&api, &IUserNetworkApi::requestFailed);
    api.logout(makeContext(QStringLiteral("req-8")));

    QCOMPARE(successes.count(), 1);
    QCOMPARE(failures.count(), 0);

    const auto frames = decodeFrames(transport.m_sentFrames);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.first().first, LOGOUT_REQ);
    QCOMPARE(frames.first().second.value(QLatin1String("requestId")).toString(),
             QStringLiteral("req-8"));
}

void NetworkAdapterTests::sendFailureReportsRequestFailed()
{
    MockTransport transport;
    BackendClient backend(&transport);
    backend.start();
    transport.setFailSend(true);
    RealUserNetworkApi api(&backend);

    QSignalSpy successes(&api, &IUserNetworkApi::loginSucceeded);
    QSignalSpy failures(&api, &IUserNetworkApi::requestFailed);
    api.loginByPhone(QStringLiteral("13800138000"), makeContext(QStringLiteral("req-9")));

    QCOMPARE(successes.count(), 0);
    QCOMPARE(failures.count(), 1);
    const ClientError error =
        qvariant_cast<ClientError>(failures.takeFirst().at(0));
    QCOMPARE(error.code, QStringLiteral("send-failed"));
}

/* 回归：首次连接被拒（无 disconnected 信号）后必须进入重连调度，
 * 恢复后能正常连上——修复前会永远卡在 Connecting */
void NetworkAdapterTests::reconnectsAfterInitialConnectFailure()
{
    MockTransport transport;
    BackendClient backend(&transport);
    backend.setReconnectIntervalMs(10);
    transport.setFailConnect(true);

    backend.start();
    QCOMPARE(backend.connectionState(), ConnectionState::Reconnecting);

    transport.setFailConnect(false);
    QTest::qWait(100);
    QCOMPARE(backend.connectionState(), ConnectionState::Connected);
}

/* 回归（合同 §5.1/§8）：缺必填字段的 217 不得产生 loginSucceeded 污染会话，
 * 且失败后在途请求已被清理，迟到的重复应答被静默丢弃 */
void NetworkAdapterTests::malformedLoginResponseDoesNotPolluteSession()
{
    MockTransport transport;
    BackendClient backend(&transport);
    backend.start();
    RealUserNetworkApi api(&backend);
    api.setRequestTimeoutMs(5000);

    QSignalSpy successes(&api, &IUserNetworkApi::loginSucceeded);
    QSignalSpy failures(&api, &IUserNetworkApi::requestFailed);
    api.loginByPhone(QStringLiteral("13800138000"), makeContext(QStringLiteral("req-10")));

    transport.simulateIncoming(MassageHandler::pack(PHONE_LOGIN_ACK, QJsonObject()));

    QCOMPARE(successes.count(), 0);
    QCOMPARE(failures.count(), 1);
    const ClientError error =
        qvariant_cast<ClientError>(failures.first().at(0));
    QCOMPARE(error.code, QStringLiteral("bad-response"));
    QVERIFY(error.retryable);

    // 在途请求已清理：再次收到 217 无人认领，不产生任何信号
    QJsonObject lateAck;
    lateAck.insert(QStringLiteral("username"), QStringLiteral("U13800138000"));
    transport.simulateIncoming(MassageHandler::pack(PHONE_LOGIN_ACK, lateAck));
    QCOMPARE(successes.count(), 0);
    QCOMPARE(failures.count(), 1);
}

/* 回归（合同 §11 v1.1）：改昵称的应答损坏（缺 nickname）时，
 * 服务端是否已生效不可知 → 必须按结果未知处理，不得当成功 */
void NetworkAdapterTests::malformedNicknameResponseIsResultUnknown()
{
    MockTransport transport;
    BackendClient backend(&transport);
    backend.start();
    RealUserNetworkApi api(&backend);
    api.setRequestTimeoutMs(5000);

    QSignalSpy successes(&api, &IUserNetworkApi::nicknameUpdateSucceeded);
    QSignalSpy failures(&api, &IUserNetworkApi::requestFailed);
    api.updateNickname(QStringLiteral("U13800138000"), QStringLiteral("Bob"),
                       makeContext(QStringLiteral("req-11")));

    transport.simulateIncoming(MassageHandler::pack(UPDNICK_ACK, QJsonObject()));

    QCOMPARE(successes.count(), 0);
    QCOMPARE(failures.count(), 1);
    const ClientError error =
        qvariant_cast<ClientError>(failures.takeFirst().at(0));
    QCOMPARE(error.code, QStringLiteral("bad-response"));
    QVERIFY(error.resultUnknown);
    QVERIFY(!error.retryable);
    QCOMPARE(error.operationId, QStringLiteral("req-11-op"));
}

/* 回归：未连接时登录请求立即失败且必须可重试——
 * 冷启动时 BackendClient 尚在自动重连，不得让用户被卡住 */
void NetworkAdapterTests::loginWhenNotConnectedIsRetryable()
{
    MockTransport transport;
    BackendClient backend(&transport);   // 不调用 start()，保持 Disconnected
    RealUserNetworkApi api(&backend);

    QSignalSpy successes(&api, &IUserNetworkApi::loginSucceeded);
    QSignalSpy failures(&api, &IUserNetworkApi::requestFailed);
    api.loginByPhone(QStringLiteral("13800138000"), makeContext(QStringLiteral("req-12")));

    QCOMPARE(successes.count(), 0);
    QCOMPARE(failures.count(), 1);
    const ClientError error =
        qvariant_cast<ClientError>(failures.first().at(0));
    QCOMPARE(error.code, QStringLiteral("not-connected"));
    QVERIFY(error.retryable);
}

QTEST_MAIN(NetworkAdapterTests)

#include "network-adapter-tests.moc"
