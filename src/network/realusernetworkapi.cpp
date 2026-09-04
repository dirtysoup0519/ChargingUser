#include "realusernetworkapi.h"

#include "backendclient.h"
#include "protocol.h"

#include <QTimer>

namespace
{

ClientError makeError(const QString &code, const QString &message, bool retryable)
{
    ClientError error;
    error.code = code;
    error.displayMessage = message;
    error.retryable = retryable;
    return error;
}

ClientError makeBizError(int errType, const QJsonObject &payload)
{
    const QString bizCode = payload.value(QStringLiteral("code")).toString();
    const QString reason = payload.value(QStringLiteral("reason")).toString();

    ClientError error;
    error.code = bizCode.isEmpty()
                     ? QStringLiteral("server-error-%1").arg(errType)
                     : bizCode;
    error.displayMessage = reason.isEmpty()
                               ? QStringLiteral("Server rejected the request (%1).").arg(errType)
                               : reason;

    // 登录失败与临时性服务端错误允许重试；参数/非法请求类不重试
    switch (errType) {
    case LOGIN_FAIL:
    case DB_ERROR:
    case DATA_NOEXIST:
        error.retryable = true;
        break;
    default:
        error.retryable = false;
        break;
    }
    return error;
}

int reqTypeForKind(RealUserNetworkApi::PendingKind kind)
{
    switch (kind) {
    case RealUserNetworkApi::PendingKind::Login:
        return PHONE_LOGIN_REQ;
    case RealUserNetworkApi::PendingKind::QueryProfile:
        return QUERY_PROFILE_REQ;
    case RealUserNetworkApi::PendingKind::UpdateNickname:
        return UPDNICK_REQ;
    }
    return 0;
}

QString requestIdOf(const QJsonObject &payload)
{
    return payload.value(QStringLiteral("requestId")).toString();
}

} // namespace

RealUserNetworkApi::RealUserNetworkApi(BackendClient *backend, QObject *parent)
    : IUserNetworkApi(parent)
    , m_backend(backend)
{
    Q_ASSERT(m_backend);

    connect(m_backend, &BackendClient::frameReceived,
            this, &RealUserNetworkApi::handleFrame);
    connect(m_backend, &BackendClient::connectionStateChanged, this,
            [this](ConnectionState state) {
        // 连接断开/重连过渡期，所有在途请求不可能再得到可靠应答
        if (state != ConnectionState::Connected) {
            failAllPending(QStringLiteral("connection-lost"),
                           QStringLiteral("Connection to server was lost."));
        }
    });
}

void RealUserNetworkApi::setRequestTimeoutMs(int timeoutMs)
{
    m_requestTimeoutMs = timeoutMs;
}

void RealUserNetworkApi::loginByPhone(const QString &phone,
                                      const RequestContext &context)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("phone"), phone);
    payload.insert(QStringLiteral("requestId"), context.requestId);
    startRequest(PendingKind::Login, payload, context, QString());
}

void RealUserNetworkApi::queryCurrentUser(const QString &userId,
                                          const RequestContext &context)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("username"), userId);
    payload.insert(QStringLiteral("requestId"), context.requestId);
    startRequest(PendingKind::QueryProfile, payload, context, userId);
}

void RealUserNetworkApi::updateNickname(const QString &userId,
                                        const QString &nickname,
                                        const RequestContext &context)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("username"), userId);
    payload.insert(QStringLiteral("nickname"), nickname);
    payload.insert(QStringLiteral("requestId"), context.requestId);
    payload.insert(QStringLiteral("operationId"), context.operationId);
    startRequest(PendingKind::UpdateNickname, payload, context, userId);
}

void RealUserNetworkApi::logout(const RequestContext &context)
{
    // 合同 §11：102 尽力发送、不等待应答，会话由调用方本地清理
    QJsonObject payload;
    payload.insert(QStringLiteral("requestId"), context.requestId);

    OperationResult result;
    result.requestId = context.requestId;
    result.operationId = context.operationId;

    if (m_backend->sendFrame(LOGOUT_REQ, payload)) {
        emit logoutSucceeded(result);
    } else {
        // 会话已由调用方本地清理，此错误仅作提示，retryable=false 表示无重试意义
        emit requestFailed(makeError(QStringLiteral("not-connected"),
                                     QStringLiteral("Not connected to server."),
                                     false));
    }
}

bool RealUserNetworkApi::startRequest(PendingKind kind, const QJsonObject &payload,
                                      const RequestContext &context,
                                      const QString &userId)
{
    if (m_backend->connectionState() != ConnectionState::Connected) {
        // 与 connection-lost 同语义：BackendClient 会自动重连，用户应当能重试
        emit requestFailed(makeError(QStringLiteral("not-connected"),
                                     QStringLiteral("Not connected to server."),
                                     true));
        return false;
    }

    // 服务只允许同类单请求（合同 §3），此处兜底防重
    for (const PendingRequest &pending : m_pendingRequests) {
        if (pending.kind == kind) {
            emit requestFailed(makeError(QStringLiteral("request-in-flight"),
                                         QStringLiteral("A similar request is already in progress."),
                                         false));
            return false;
        }
    }

    if (!m_backend->sendFrame(reqTypeForKind(kind), payload)) {
        emit requestFailed(makeError(QStringLiteral("send-failed"),
                                     QStringLiteral("Failed to send request."),
                                     true));
        return false;
    }

    PendingRequest pending;
    pending.kind = kind;
    pending.requestId = context.requestId;
    pending.operationId = context.operationId;
    pending.userId = userId;

    pending.timer = new QTimer(this);
    pending.timer->setSingleShot(true);
    connect(pending.timer, &QTimer::timeout, this, [this, requestId = context.requestId] {
        handleTimeout(requestId);
    });
    pending.timer->start(m_requestTimeoutMs);

    m_pendingRequests.insert(context.requestId, pending);
    m_pendingOrder.append(context.requestId);
    return true;
}

void RealUserNetworkApi::handleFrame(int msgType, const QJsonObject &payload)
{
    if (msgType >= DATA_NOEXIST && msgType <= PARAM_ERROR) {
        handleServerError(msgType, payload);
        return;
    }

    PendingKind kind;
    if (msgType == PHONE_LOGIN_ACK) {
        kind = PendingKind::Login;
    } else if (msgType == PROFILE_ACK) {
        kind = PendingKind::QueryProfile;
    } else if (msgType == UPDNICK_ACK) {
        kind = PendingKind::UpdateNickname;
    } else {
        return;   // 未知消息码不得污染会话（合同 §8 测试基线）
    }

    PendingRequest *pending = findPendingForResponse(kind, requestIdOf(payload));
    if (pending == nullptr) {
        return;   // 迟到响应或未匹配，静默丢弃
    }

    // 合同 §5.1：不能以“得到 QJsonObject”作为成功条件，必须校验必填字段；
    // 缺失字段的应答不得污染会话（合同 §8 测试基线）
    if (!successPayloadValid(kind, payload)) {
        ClientError error;
        if (kind == PendingKind::UpdateNickname) {
            // 变更操作的应答损坏 → 无法得知服务端是否已生效 → 结果未知
            error = makeTimeoutError(kind, *pending);
            error.code = QStringLiteral("bad-response");
        } else {
            error = makeError(QStringLiteral("bad-response"),
                              QStringLiteral("Server response was malformed."),
                              true);
            error.requestId = pending->requestId;
            error.operationId = pending->operationId;
        }
        failPending(*pending, error);
        return;
    }

    const PendingRequest request = *pending;
    takePending(request.requestId, kind, nullptr);

    switch (request.kind) {
    case PendingKind::Login: {
        LoginResult result;
        result.requestId = request.requestId;
        result.isNewUser = payload.value(QStringLiteral("autoRegistered")).toBool(false);
        // v1.1：profileCompleted 由客户端推导，217 不携带该字段
        result.profileCompleted = !result.isNewUser;
        result.session.profile.userId = payload.value(QStringLiteral("username")).toString();
        result.session.profile.phone = payload.value(QStringLiteral("phone")).toString();
        result.session.profile.nickname = payload.value(QStringLiteral("nickname")).toString();
        result.session.accountStatus =
            parseStatus(payload.value(QStringLiteral("status")).toString());
        result.session.authenticated = true;
        emit loginSucceeded(result);
        break;
    }
    case PendingKind::QueryProfile: {
        UserProfileResult result;
        result.requestId = request.requestId;
        result.operationId = request.operationId;
        result.profile.userId = payload.value(QStringLiteral("username")).toString();
        result.profile.phone = payload.value(QStringLiteral("phone")).toString();
        result.profile.nickname = payload.value(QStringLiteral("nickname")).toString();
        result.accountStatus =
            parseStatus(payload.value(QStringLiteral("status")).toString());
        emit currentUserQuerySucceeded(result);
        break;
    }
    case PendingKind::UpdateNickname: {
        UserProfileResult result;
        result.requestId = request.requestId;
        result.operationId = request.operationId;
        result.profile.userId = request.userId;
        result.profile.nickname = payload.value(QStringLiteral("nickname")).toString();
        emit nicknameUpdateSucceeded(result);
        break;
    }
    }
}

void RealUserNetworkApi::handleServerError(int errType, const QJsonObject &payload)
{
    ClientError error = makeBizError(errType, payload);

    PendingRequest *pending = nullptr;
    const QString echoedRequestId = requestIdOf(payload);
    if (!echoedRequestId.isEmpty()) {
        pending = findPendingForResponseByRequestId(echoedRequestId);
    } else {
        pending = findMostRecentPending();   // v1.1：3xx 归属最近在途请求
    }
    if (pending == nullptr) {
        return;   // 迟到错误，丢弃
    }

    error.requestId = pending->requestId;
    error.operationId = pending->operationId;
    failPending(*pending, error);
}

void RealUserNetworkApi::handleTimeout(const QString &requestId)
{
    PendingRequest *pending = findPendingByRequestId(requestId);
    if (pending == nullptr) {
        return;
    }

    ClientError error = makeTimeoutError(pending->kind, *pending);
    failPending(*pending, error);
}

ClientError RealUserNetworkApi::makeTimeoutError(PendingKind kind,
                                                 const PendingRequest &pending) const
{
    // 合同 §11：查询超时=可重试失败；变更操作超时=结果未知，禁止直接重做
    if (kind == PendingKind::UpdateNickname) {
        ClientError error = makeError(QStringLiteral("result-unknown"),
                                      QStringLiteral("Nickname update result is unknown."),
                                      false);
        error.resultUnknown = true;
        error.requestId = pending.requestId;
        error.operationId = pending.operationId;
        return error;
    }
    ClientError error = makeError(QStringLiteral("request-timeout"),
                                  QStringLiteral("Request timed out."),
                                  true);
    error.requestId = pending.requestId;
    error.operationId = pending.operationId;
    return error;
}

void RealUserNetworkApi::failPending(PendingRequest pending, const ClientError &error)
{
    if (pending.timer != nullptr) {
        pending.timer->stop();
        pending.timer->deleteLater();
    }
    m_pendingRequests.remove(pending.requestId);
    m_pendingOrder.removeAll(pending.requestId);
    emit requestFailed(error);
}

void RealUserNetworkApi::failAllPending(const QString &code, const QString &message)
{
    const QList<QString> requestIds = m_pendingOrder;
    for (const QString &requestId : requestIds) {
        PendingRequest *pending = findPendingByRequestId(requestId);
        if (pending != nullptr) {
            failPending(*pending, makeError(code, message, true));
        }
    }
}

bool RealUserNetworkApi::takePending(const QString &requestId, PendingKind expectedKind,
                                     PendingRequest *out)
{
    PendingRequest *pending = findPendingByRequestId(requestId);
    if (pending == nullptr || pending->kind != expectedKind) {
        return false;
    }
    if (out != nullptr) {
        *out = *pending;
    }
    if (pending->timer != nullptr) {
        pending->timer->stop();
        pending->timer->deleteLater();
    }
    m_pendingRequests.remove(requestId);
    m_pendingOrder.removeAll(requestId);
    return true;
}

RealUserNetworkApi::PendingRequest *RealUserNetworkApi::findPendingByRequestId(const QString &requestId)
{
    const auto iterator = m_pendingRequests.find(requestId);
    return iterator == m_pendingRequests.end() ? nullptr : &iterator.value();
}

RealUserNetworkApi::PendingRequest *RealUserNetworkApi::findPendingForResponseByRequestId(const QString &requestId)
{
    return findPendingByRequestId(requestId);
}

RealUserNetworkApi::PendingRequest *RealUserNetworkApi::findPendingForResponse(
    PendingKind kind, const QString &echoedRequestId)
{
    // 优先按服务端回显 requestId 精确匹配（v2.1：218/219/217 均回显）
    if (!echoedRequestId.isEmpty()) {
        PendingRequest *pending = findPendingByRequestId(echoedRequestId);
        if (pending != nullptr && pending->kind == kind) {
            return pending;
        }
        return nullptr;   // 回显了 requestId 却不属于任何在途请求 → 迟到响应
    }

    // 回退：该应答类型对应的最早在途请求（M1 同类防重保证最多一个）
    for (const QString &requestId : m_pendingOrder) {
        PendingRequest *pending = findPendingByRequestId(requestId);
        if (pending != nullptr && pending->kind == kind) {
            return pending;
        }
    }
    return nullptr;
}

RealUserNetworkApi::PendingRequest *RealUserNetworkApi::findMostRecentPending()
{
    for (auto iterator = m_pendingOrder.rbegin(); iterator != m_pendingOrder.rend(); ++iterator) {
        PendingRequest *pending = findPendingByRequestId(*iterator);
        if (pending != nullptr) {
            return pending;
        }
    }
    return nullptr;
}

AccountStatus RealUserNetworkApi::parseStatus(const QString &status)
{
    if (status == QLatin1String("Normal")) {
        return AccountStatus::Normal;
    }
    if (status == QLatin1String("Frozen")) {
        return AccountStatus::Frozen;
    }
    return AccountStatus::Unknown;
}

bool RealUserNetworkApi::successPayloadValid(PendingKind kind,
                                             const QJsonObject &payload)
{
    // 217/218 必须携带 username；219 必须携带生效后的 nickname。
    // status 等其余字段缺失时按既有语义降级（Unknown 按受限处理），不视为协议错误。
    switch (kind) {
    case PendingKind::Login:
    case PendingKind::QueryProfile:
        return !payload.value(QStringLiteral("username")).toString().isEmpty();
    case PendingKind::UpdateNickname:
        return !payload.value(QStringLiteral("nickname")).toString().isEmpty();
    }
    return false;
}
