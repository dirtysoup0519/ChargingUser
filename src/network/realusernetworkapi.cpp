#include "realusernetworkapi.h"

#include "backendclient.h"
#include "protocol.h"

#include <QJsonArray>
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
    QString reason = payload.value(QStringLiteral("err")).toString();
    if (reason.isEmpty()) {
        reason = payload.value(QStringLiteral("reason")).toString();
    }

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
    case RealUserNetworkApi::PendingKind::CredentialLogin:
        return LOGIN_REQ;
    case RealUserNetworkApi::PendingKind::QueryProfile:
        return GETDATA;
    case RealUserNetworkApi::PendingKind::UpdateNickname:
    case RealUserNetworkApi::PendingKind::UpdateAvatar:
    case RealUserNetworkApi::PendingKind::UpdatePassword:
        return PROFILE_UPD_REQ;
    case RealUserNetworkApi::PendingKind::Logout:
        return LOGOUT_REQ;
    }
    return 0;
}

QString requestIdOf(const QJsonObject &payload)
{
    return payload.value(QStringLiteral("requestId")).toString();
}

/* 所有失败路径都必须回填 requestId/operationId（合同 §12.1 第 3 条）：
 * UserService 先登记在途请求再调网络层，错误不带 ID 上层就无法释放，
 * 用户会被“请求进行中”永久卡住 */
ClientError failedRequestError(const RequestContext &context, const QString &code,
                               const QString &message, bool retryable)
{
    ClientError error = makeError(code, message, retryable);
    error.requestId = context.requestId;
    error.operationId = context.operationId;
    return error;
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

void RealUserNetworkApi::loginByCredentials(const QString &username,
                                            const QString &password,
                                            const RequestContext &context)
{
    // v2.6: role 由服务端会话/数据库决定；手机号自动注册用户的初始密码为手机号。
    QString account = username.trimmed();
    // 手机号免密登录自动创建的数据库用户名为 U+手机号；密码登录页允许用户
    // 直接输入手机号，因此这里转换为协议 101 所需的 username。
    if (account.size() == 11 && account.startsWith(QLatin1Char('1')))
        account.prepend(QStringLiteral("U"));
    QJsonObject payload{{QStringLiteral("username"), account},
                        {QStringLiteral("password"), password},
                        {QStringLiteral("requestId"), context.requestId}};
    startRequest(PendingKind::CredentialLogin, payload, context, QString());
}

void RealUserNetworkApi::queryCurrentUser(const QString &userId,
                                          const RequestContext &context)
{
    QJsonObject condition;
    condition.insert(QStringLiteral("username"), userId);

    QJsonObject payload;
    payload.insert(QStringLiteral("table"), QString::fromLatin1(TBL_USER));
    payload.insert(QStringLiteral("cond"), condition);
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
    startRequest(PendingKind::UpdateNickname, payload, context, userId, nickname);
}

void RealUserNetworkApi::updateAvatar(const QString &userId,
                                      const QString &avatarDataUri,
                                      const RequestContext &context)
{
    QJsonObject payload{{QStringLiteral("username"), userId},
                        {QStringLiteral("avatar"), avatarDataUri},
                        {QStringLiteral("requestId"), context.requestId},
                        {QStringLiteral("operationId"), context.operationId}};
    startRequest(PendingKind::UpdateAvatar, payload, context, userId,
                 QString(), avatarDataUri);
}

void RealUserNetworkApi::changePassword(const QString &userId,
                                        const QString &oldPassword,
                                        const QString &newPassword,
                                        const RequestContext &context)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("username"), userId);
    if (!oldPassword.isEmpty())
        payload.insert(QStringLiteral("oldPassword"), oldPassword);
    payload.insert(QStringLiteral("newPassword"), newPassword);
    payload.insert(QStringLiteral("requestId"), context.requestId);
    payload.insert(QStringLiteral("operationId"), context.operationId);
    startRequest(PendingKind::UpdatePassword, payload, context, userId);
}

void RealUserNetworkApi::logout(const RequestContext &context)
{
    // v2.6.3：本地会话由 UserService 立即清理，但网络操作须等待 202，
    // 以保证同一 TCP 连接上的后续登录不会抢在服务端清除旧身份之前。
    QJsonObject payload;
    payload.insert(QStringLiteral("requestId"), context.requestId);
    startRequest(PendingKind::Logout, payload, context, QString());
}

bool RealUserNetworkApi::startRequest(PendingKind kind, const QJsonObject &payload,
                                      const RequestContext &context,
                                      const QString &userId,
                                      const QString &requestedNickname,
                                      const QString &requestedAvatar)
{
    if (m_backend->connectionState() != ConnectionState::Connected) {
        // 与 connection-lost 同语义：BackendClient 会自动重连，用户应当能重试
        emit requestFailed(failedRequestError(context,
                                              QStringLiteral("not-connected"),
                                              QStringLiteral("Not connected to server."),
                                              kind != PendingKind::Logout));
        return false;
    }

    // 服务只允许同类单请求（合同 §3），此处兜底防重
    for (const PendingRequest &pending : m_pendingRequests) {
        const bool bothProfileMutations =
            (kind == PendingKind::UpdateNickname
             || kind == PendingKind::UpdateAvatar
             || kind == PendingKind::UpdatePassword)
            && (pending.kind == PendingKind::UpdateNickname
                || pending.kind == PendingKind::UpdateAvatar
                || pending.kind == PendingKind::UpdatePassword);
        if (pending.kind == kind || bothProfileMutations) {
            emit requestFailed(failedRequestError(context,
                                                  QStringLiteral("request-in-flight"),
                                                  QStringLiteral("A similar request is already in progress."),
                                                  false));
            return false;
        }
    }

    if (!m_backend->sendFrame(reqTypeForKind(kind), payload)) {
        emit requestFailed(failedRequestError(context,
                                              QStringLiteral("send-failed"),
                                              QStringLiteral("Failed to send request."),
                                              kind != PendingKind::Logout));
        return false;
    }

    PendingRequest pending;
    pending.kind = kind;
    pending.requestId = context.requestId;
    pending.operationId = context.operationId;
    pending.userId = userId;
    pending.requestedNickname = requestedNickname;
    pending.requestedAvatar = requestedAvatar;

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
    if (msgType >= DATA_NOEXIST && msgType <= OP_FORBIDDEN) {
        handleServerError(msgType, payload);
        return;
    }

    PendingKind kind;
    if (msgType == PHONE_LOGIN_ACK) {
        kind = PendingKind::Login;
    } else if (msgType == LOGIN_ACK) {
        kind = PendingKind::CredentialLogin;
    } else if (msgType == DATA) {
        kind = PendingKind::QueryProfile;
    } else if (msgType == PROFILE_UPD_ACK) {
        const QString responseRequestId = requestIdOf(payload);
        if (!responseRequestId.isEmpty()) {
            PendingRequest *profilePending =
                findPendingForResponseByRequestId(responseRequestId);
            if (profilePending == nullptr
                || (profilePending->kind != PendingKind::UpdateNickname
                    && profilePending->kind != PendingKind::UpdateAvatar
                    && profilePending->kind != PendingKind::UpdatePassword)) {
                return;
            }
            kind = profilePending->kind;
        } else {
            PendingRequest *profilePending = nullptr;
            for (const QString &requestId : m_pendingOrder) {
                PendingRequest *candidate = findPendingByRequestId(requestId);
                if (candidate != nullptr
                    && (candidate->kind == PendingKind::UpdateNickname
                        || candidate->kind == PendingKind::UpdateAvatar
                        || candidate->kind == PendingKind::UpdatePassword)) {
                    profilePending = candidate;
                    break;
                }
            }
            if (profilePending == nullptr)
                return;
            kind = profilePending->kind;
        }
    } else if (msgType == LOGOUT_ACK) {
        kind = PendingKind::Logout;
    } else {
        return;   // 未知消息码不得污染会话（合同 §8 测试基线）
    }

    PendingRequest *pending = findPendingForResponse(kind, requestIdOf(payload));
    if (pending == nullptr) {
        return;   // 迟到响应或未匹配，静默丢弃
    }

    QJsonObject response = payload;
    if (kind == PendingKind::QueryProfile) {
        const QJsonArray rows = payload.value(QStringLiteral("data")).toArray();
        response = QJsonObject();
        for (const QJsonValue &value : rows) {
            const QJsonObject row = value.toObject();
            if (row.value(QStringLiteral("username")).toString()
                == pending->userId) {
                response = row;
                break;
            }
        }
    }

    // 合同 §5.1：不能以“得到 QJsonObject”作为成功条件，必须校验必填字段；
    // 缺失字段的应答不得污染会话（合同 §8 测试基线）
    if (!successPayloadValid(kind, response, pending->userId)) {
        ClientError error;
        if (kind == PendingKind::UpdateNickname
            || kind == PendingKind::UpdateAvatar
            || kind == PendingKind::UpdatePassword) {
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
    case PendingKind::Login:
    case PendingKind::CredentialLogin: {
        LoginResult result;
        result.requestId = request.requestId;
        result.isNewUser = msgType == PHONE_LOGIN_ACK
                               && payload.value(QStringLiteral("autoRegistered")).toBool(false);
        // v1.1：profileCompleted 由客户端推导，217 不携带该字段
        result.profileCompleted = !result.isNewUser;
        result.session.profile.userId = response.value(QStringLiteral("username")).toString();
        result.session.profile.phone = response.value(QStringLiteral("phone")).toString();
        result.session.profile.nickname = response.value(QStringLiteral("nickname")).toString();
        result.session.profile.avatarKey = response.value(QStringLiteral("avatar")).toString();
        if (response.contains(QStringLiteral("balanceCents"))) {
            bool balanceOk = false;
            const qint64 balance = response.value(QStringLiteral("balanceCents"))
                                       .toVariant().toLongLong(&balanceOk);
            if (balanceOk) {
                result.session.profile.balanceCents = balance;
            }
        }
        result.session.accountStatus =
            parseStatus(response.value(QStringLiteral("status")).toString());
        result.session.authenticated = true;
        emit loginSucceeded(result);
        break;
    }
    case PendingKind::QueryProfile: {
        UserProfileResult result;
        result.requestId = request.requestId;
        result.operationId = request.operationId;
        result.profile.userId = response.value(QStringLiteral("username")).toString();
        result.profile.phone = response.value(QStringLiteral("phone")).toString();
        result.profile.nickname = response.value(QStringLiteral("nickname")).toString();
        result.profile.avatarKey = response.value(QStringLiteral("avatar")).toString();
        if (response.contains(QStringLiteral("balanceCents"))) {
            bool balanceOk = false;
            const qint64 balance = response.value(QStringLiteral("balanceCents"))
                                       .toVariant().toLongLong(&balanceOk);
            if (balanceOk) {
                result.profile.balanceCents = balance;
            }
        }
        result.accountStatus =
            parseStatus(response.value(QStringLiteral("status")).toString());
        emit currentUserQuerySucceeded(result);
        break;
    }
    case PendingKind::UpdateNickname: {
        UserProfileResult result;
        result.requestId = request.requestId;
        result.operationId = request.operationId;
        result.profile.userId = request.userId;
        result.profile.nickname = request.requestedNickname;
        emit nicknameUpdateSucceeded(result);
        break;
    }
    case PendingKind::UpdateAvatar: {
        UserProfileResult result;
        result.requestId = request.requestId;
        result.operationId = request.operationId;
        result.profile.userId = request.userId;
        result.profile.avatarKey = request.requestedAvatar;
        emit avatarUpdateSucceeded(result);
        break;
    }
    case PendingKind::UpdatePassword: {
        OperationResult result;
        result.requestId = request.requestId;
        result.operationId = request.operationId;
        emit passwordChangeSucceeded(result);
        break;
    }
    case PendingKind::Logout: {
        OperationResult result;
        result.requestId = request.requestId;
        result.operationId = request.operationId;
        emit logoutSucceeded(result);
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
        // v1.2：无回显的 3xx 仅在全局恰好一个在途请求时才归属。
        // 查询与改昵称可并发，且存在“请求 A 超时后用户重试 B”的场景，
        // 归属“最近请求”会把错误算到无关请求头上（审查问题 2），宁可丢弃交超时兜底。
        pending = findSolePending();
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
    if (kind == PendingKind::UpdateNickname
        || kind == PendingKind::UpdateAvatar
        || kind == PendingKind::UpdatePassword) {
        ClientError error = makeError(QStringLiteral("result-unknown"),
                                      kind == PendingKind::UpdatePassword
                                          ? QStringLiteral("Password change result is unknown. Sign in again to verify it.")
                                          : kind == PendingKind::UpdateAvatar
                                                ? QStringLiteral("Avatar update result is unknown. Refresh the profile before retrying.")
                                                : QStringLiteral("Nickname update result is unknown."),
                                      false);
        error.resultUnknown = true;
        error.requestId = pending.requestId;
        error.operationId = pending.operationId;
        return error;
    }
    if (kind == PendingKind::Logout) {
        ClientError error = makeError(QStringLiteral("request-timeout"),
                                      QStringLiteral("Logout confirmation timed out."),
                                      false);
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
        if (pending == nullptr) {
            continue;
        }
        // 失败错误必须回填 requestId/operationId，否则 UserService 无法
        // 释放对应在途请求，用户会被“请求进行中”永久卡住（审查问题 1）。
        // 断线时变更操作的结果不可知（服务端可能已生效），按结果未知处理，
        // 不得标成普通可重试错误（合同 §11 v1.2）。
        ClientError error = makeError(
            code, message,
            pending->kind != PendingKind::UpdateNickname
                && pending->kind != PendingKind::UpdateAvatar
                && pending->kind != PendingKind::UpdatePassword
                && pending->kind != PendingKind::Logout);
        error.requestId = pending->requestId;
        error.operationId = pending->operationId;
        if (pending->kind == PendingKind::UpdateNickname
            || pending->kind == PendingKind::UpdateAvatar
            || pending->kind == PendingKind::UpdatePassword) {
            error.resultUnknown = true;
        }
        failPending(*pending, error);
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

RealUserNetworkApi::PendingRequest *RealUserNetworkApi::findSolePending()
{
    // 0 个：迟到错误，丢弃；≥2 个：无法可靠归属，丢弃（交由各自超时兜底）
    if (m_pendingRequests.size() != 1) {
        return nullptr;
    }
    return &m_pendingRequests.begin().value();
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
                                             const QJsonObject &payload,
                                             const QString &requestedUserId)
{
    // 217 必须携带 username（会话尚未建立，phone 缺失不产生污染，按降级处理）；
    // 200 的 user 行必须携带 username 且必须回的是请求的那个用户（防串号），
    //     且必须携带 phone（缺失会把会话手机号清空——审查问题 5）；
    // 228 必须携带 ok=true，并回显实际生效账户的 username；
    // status 等其余字段缺失时按既有语义降级（Unknown 按受限处理），不视为协议错误。
    const QString username = payload.value(QStringLiteral("username")).toString();
    switch (kind) {
    case PendingKind::Login:
    case PendingKind::CredentialLogin:
        return !username.isEmpty();
    case PendingKind::QueryProfile:
        return !username.isEmpty()
               && username == requestedUserId
               && !payload.value(QStringLiteral("phone")).toString().isEmpty();
    case PendingKind::UpdateNickname:
    case PendingKind::UpdateAvatar:
    case PendingKind::UpdatePassword:
        return payload.value(QStringLiteral("ok")).toBool(false)
               && username == requestedUserId;
    case PendingKind::Logout:
        return payload.value(QStringLiteral("ok")).toBool(false);
    }
    return false;
}
