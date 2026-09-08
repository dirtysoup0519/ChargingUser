/* RealUserNetworkApi 适配器测试
 * 用 MockTransport 模拟 socket：直接注入协议帧、控制连接状态。
 * 覆盖当前服务端 v2.4 协议的适配器语义：
 *  - 登录请求携带 requestId；未冻结的资料接口不得借用高权限通道
 *  - 登录应答优先按 requestId 匹配，无回显时按类型回退
 *  - 218 是重启确认，不得误解析为用户资料应答
 *  - 3xx 映射业务错误码；断线使在途请求失败
 *  - 退出登录尽力发送，不等待应答
 */
#include "massagehandler.h"
#include "network/backendclient.h"
#include "network/inetworktransport.h"
#include "network/realusernetworkapi.h"
#include "protocol.h"

#include <QSignalSpy>

#include <algorithm>
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

/* 过滤连接时自动发出的 107 存活心跳：心跳属于生命周期帧，
 * 业务用例只断言业务帧的条数与顺序。 */
QList<QPair<int, QJsonObject>> businessFrames(const QList<QByteArray> &bytesList)
{
    auto frames = decodeFrames(bytesList);
    frames.erase(std::remove_if(frames.begin(), frames.end(),
                                [](const QPair<int, QJsonObject> &frame) {
                                    return frame.first == HEARTBEAT;
                                }),
                 frames.end());
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
    void queryProfileReportsUnsupportedProtocol();
    void nicknameUpdateReportsUnsupportedProtocol();
    void disconnectFailsPendingRequests();
    void restartAckIsIgnoredByUserAdapter();
    void logoutSendsImmediatelyWithoutAck();
    void sendFailureReportsRequestFailed();
    void reconnectsAfterInitialConnectFailure();
    void malformedLoginResponseDoesNotPolluteSession();
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
    const auto frames = businessFrames(transport.m_sentFrames);

    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.first().first, PHONE_LOGIN_REQ);
    const QJsonObject payload = frames.first().second;
    QCOMPARE(payload.value(QLatin1String("phone")).toString(),
             QStringLiteral("13800138000"));
    QCOMPARE(payload.value(QLatin1String("requestId")).toString(),
             QStringLiteral("req-x"));
    // 联调约定：password=手机号 随载荷携带，服务端自动注册用默认密码。
    QCOMPARE(payload.value(QLatin1String("password")).toString(),
             QStringLiteral("13800138000"));
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
    err.insert(QStringLiteral("err"), QStringLiteral("bad phone"));
    transport.simulateIncoming(MassageHandler::pack(PARAM_ERROR, err));

    QCOMPARE(failures.count(), 1);
    const ClientError error =
        qvariant_cast<ClientError>(failures.takeFirst().at(0));
    QCOMPARE(error.code, QStringLiteral(BIZ_ERR_PARAM));
    QCOMPARE(error.displayMessage, QStringLiteral("bad phone"));
    QVERIFY(!error.retryable);
    QCOMPARE(error.requestId, QStringLiteral("req-3"));
}

void NetworkAdapterTests::queryProfileReportsUnsupportedProtocol()
{
    MockTransport transport;
    BackendClient backend(&transport);
    backend.start();
    RealUserNetworkApi api(&backend);
    QSignalSpy failures(&api, &IUserNetworkApi::requestFailed);
    api.queryCurrentUser(QStringLiteral("U13800138000"),
                         makeContext(QStringLiteral("req-4")));

    QCOMPARE(failures.count(), 1);
    const ClientError error =
        qvariant_cast<ClientError>(failures.takeFirst().at(0));
    QCOMPARE(error.code, QStringLiteral("unsupported-protocol"));
    QCOMPARE(error.requestId, QStringLiteral("req-4"));
    QCOMPARE(error.operationId, QStringLiteral("req-4-op"));
    QVERIFY(!error.retryable);
    QVERIFY(!error.resultUnknown);
    QVERIFY(businessFrames(transport.m_sentFrames).isEmpty());
}

void NetworkAdapterTests::nicknameUpdateReportsUnsupportedProtocol()
{
    MockTransport transport;
    BackendClient backend(&transport);
    backend.start();
    RealUserNetworkApi api(&backend);
    QSignalSpy failures(&api, &IUserNetworkApi::requestFailed);
    api.updateNickname(QStringLiteral("U13800138000"), QStringLiteral("老王"),
                       makeContext(QStringLiteral("req-5")));

    QCOMPARE(failures.count(), 1);
    const ClientError error =
        qvariant_cast<ClientError>(failures.takeFirst().at(0));
    QCOMPARE(error.code, QStringLiteral("unsupported-protocol"));
    QCOMPARE(error.requestId, QStringLiteral("req-5"));
    QCOMPARE(error.operationId, QStringLiteral("req-5-op"));
    QVERIFY(!error.retryable);
    QVERIFY(!error.resultUnknown);
    QVERIFY(businessFrames(transport.m_sentFrames).isEmpty());
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
    // 合同 §12.1 第 3 条：断线错误必须携带 requestId，否则上层无法释放在途请求
    QCOMPARE(error.requestId, QStringLiteral("req-7"));
    QCOMPARE(error.operationId, QStringLiteral("req-7-op"));
    QVERIFY(error.retryable);
    QVERIFY(!error.resultUnknown);
}

void NetworkAdapterTests::restartAckIsIgnoredByUserAdapter()
{
    MockTransport transport;
    BackendClient backend(&transport);
    backend.start();
    RealUserNetworkApi api(&backend);

    QSignalSpy profileSuccesses(&api, &IUserNetworkApi::currentUserQuerySucceeded);
    QSignalSpy failures(&api, &IUserNetworkApi::requestFailed);
    QJsonObject ack;
    ack.insert(QStringLiteral("chargerCode"), QStringLiteral("P001"));
    ack.insert(QStringLiteral("accepted"), true);
    transport.simulateIncoming(MassageHandler::pack(RESTART_ACK, ack));

    QCOMPARE(profileSuccesses.count(), 0);
    QCOMPARE(failures.count(), 0);
}

#if 0 // 旧 117/118/218/219 专用资料协议测试；服务端 v2.4 未提供这些接口
void NetworkAdapterTests::disconnectDuringNicknameUpdateIsResultUnknown()
{
    MockTransport transport;
    BackendClient backend(&transport);
    backend.start();
    RealUserNetworkApi api(&backend);
    api.setRequestTimeoutMs(5000);

    QSignalSpy failures(&api, &IUserNetworkApi::requestFailed);
    api.updateNickname(QStringLiteral("U13800138000"), QStringLiteral("老王"),
                       makeContext(QStringLiteral("req-13")));
    transport.simulateDisconnected();

    QCOMPARE(failures.count(), 1);
    const ClientError error =
        qvariant_cast<ClientError>(failures.takeFirst().at(0));
    QCOMPARE(error.code, QStringLiteral("connection-lost"));
    QCOMPARE(error.requestId, QStringLiteral("req-13"));
    QCOMPARE(error.operationId, QStringLiteral("req-13-op"));
    // 断线时 118 结果不可知（服务端可能已生效）：按结果未知处理，不得自动重试
    QVERIFY(error.resultUnknown);
    QVERIFY(!error.retryable);
}

void NetworkAdapterTests::ambiguousErrorWithoutEchoIsDropped()
{
    MockTransport transport;
    BackendClient backend(&transport);
    backend.start();
    RealUserNetworkApi api(&backend);
    api.setRequestTimeoutMs(5000);

    QSignalSpy failures(&api, &IUserNetworkApi::requestFailed);
    api.queryCurrentUser(QStringLiteral("U13800138000"),
                         makeContext(QStringLiteral("req-14")));
    api.updateNickname(QStringLiteral("U13800138000"), QStringLiteral("老王"),
                       makeContext(QStringLiteral("req-15")));

    // 无回显 requestId 的 3xx：两个在途请求无法可靠归属 → 丢弃，交超时兜底
    QJsonObject err;
    err.insert(QStringLiteral("code"), BIZ_ERR_DB);
    transport.simulateIncoming(MassageHandler::pack(DB_ERROR, err));

    QCOMPARE(failures.count(), 0);
}

void NetworkAdapterTests::profileAckForWrongUserIsRejected()
{
    MockTransport transport;
    BackendClient backend(&transport);
    backend.start();
    RealUserNetworkApi api(&backend);
    api.setRequestTimeoutMs(5000);

    QSignalSpy successes(&api, &IUserNetworkApi::currentUserQuerySucceeded);
    QSignalSpy failures(&api, &IUserNetworkApi::requestFailed);
    api.queryCurrentUser(QStringLiteral("U13800138000"),
                         makeContext(QStringLiteral("req-16")));

    // username 与请求不符：疑似串号，按应答损坏处理（合同 §12.1 第 6 条）
    QJsonObject ack;
    ack.insert(QStringLiteral("username"), QStringLiteral("U99999999999"));
    ack.insert(QStringLiteral("phone"), QStringLiteral("13800138000"));
    ack.insert(QStringLiteral("requestId"), QStringLiteral("req-16"));
    transport.simulateIncoming(MassageHandler::pack(PROFILE_ACK, ack));

    QCOMPARE(successes.count(), 0);
    QCOMPARE(failures.count(), 1);
    const ClientError error =
        qvariant_cast<ClientError>(failures.takeFirst().at(0));
    QCOMPARE(error.code, QStringLiteral("bad-response"));
    QCOMPARE(error.requestId, QStringLiteral("req-16"));
    QVERIFY(error.retryable);
}

void NetworkAdapterTests::profileAckWithoutPhoneIsRejected()
{
    MockTransport transport;
    BackendClient backend(&transport);
    backend.start();
    RealUserNetworkApi api(&backend);
    api.setRequestTimeoutMs(5000);

    QSignalSpy successes(&api, &IUserNetworkApi::currentUserQuerySucceeded);
    QSignalSpy failures(&api, &IUserNetworkApi::requestFailed);
    api.queryCurrentUser(QStringLiteral("U13800138000"),
                         makeContext(QStringLiteral("req-17")));

    // 缺 phone 的 218 不得整体覆盖会话（否则清空手机号，审查问题 5）
    QJsonObject ack;
    ack.insert(QStringLiteral("username"), QStringLiteral("U13800138000"));
    ack.insert(QStringLiteral("requestId"), QStringLiteral("req-17"));
    transport.simulateIncoming(MassageHandler::pack(PROFILE_ACK, ack));

    QCOMPARE(successes.count(), 0);
    QCOMPARE(failures.count(), 1);
    const ClientError error =
        qvariant_cast<ClientError>(failures.takeFirst().at(0));
    QCOMPARE(error.code, QStringLiteral("bad-response"));
    QCOMPARE(error.requestId, QStringLiteral("req-17"));
    QVERIFY(error.retryable);
}

void NetworkAdapterTests::nicknameAckWithoutOkIsRejected()
{
    MockTransport transport;
    BackendClient backend(&transport);
    backend.start();
    RealUserNetworkApi api(&backend);
    api.setRequestTimeoutMs(5000);

    QSignalSpy successes(&api, &IUserNetworkApi::nicknameUpdateSucceeded);
    QSignalSpy failures(&api, &IUserNetworkApi::requestFailed);
    api.updateNickname(QStringLiteral("U13800138000"), QStringLiteral("老王"),
                       makeContext(QStringLiteral("req-18")));

    // 合同 §11.1：219 必须携带 ok=true；nickname 非空但缺 ok → 应答损坏 → 结果未知
    QJsonObject ack;
    ack.insert(QStringLiteral("nickname"), QStringLiteral("老王"));
    ack.insert(QStringLiteral("requestId"), QStringLiteral("req-18"));
    transport.simulateIncoming(MassageHandler::pack(UPDNICK_ACK, ack));

    QCOMPARE(successes.count(), 0);
    QCOMPARE(failures.count(), 1);
    const ClientError error =
        qvariant_cast<ClientError>(failures.takeFirst().at(0));
    QCOMPARE(error.code, QStringLiteral("bad-response"));
    QCOMPARE(error.requestId, QStringLiteral("req-18"));
    QVERIFY(error.resultUnknown);
    QVERIFY(!error.retryable);
}

#endif

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

    const auto frames = businessFrames(transport.m_sentFrames);
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
    // 合同 §12.1 第 3 条：所有失败路径必须回填 requestId
    QCOMPARE(error.requestId, QStringLiteral("req-9"));
    QCOMPARE(error.operationId, QStringLiteral("req-9-op"));
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
#if 0 // 旧 219 昵称应答测试
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
#endif

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
    // 合同 §12.1 第 3 条：所有失败路径必须回填 requestId
    QCOMPARE(error.requestId, QStringLiteral("req-12"));
    QCOMPARE(error.operationId, QStringLiteral("req-12-op"));
}

QTEST_MAIN(NetworkAdapterTests)

#include "network-adapter-tests.moc"
