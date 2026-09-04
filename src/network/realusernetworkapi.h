#pragma once

#include "modules/user/iusernetworkapi.h"

#include <QHash>
#include <QJsonObject>
#include <QList>

class BackendClient;
class QTimer;

/* IUserNetworkApi 的真实适配器（合同 §7 步骤 7，协议 v2.1）：
 *  - 把 M1 类型化接口翻译为 116/117/118/102 报文，经 BackendClient 收发；
 *  - 请求关联按合同 §11 v1.1：优先用服务端回显的 requestId 匹配，
 *    无回显时回退到“该应答类型对应的最早在途请求”（M1 同类请求防重保证无歧义）；
 *  - 查询超时 = 可重试失败；改昵称超时 = 结果未知（resultUnknown），恢复由 M4 负责；
 *  - 退出登录尽力发送，不等待应答（合同 §3）。
 * 本类不维护会话世代——迟到响应过滤由 UserService 按 requestId/世代完成。
 */
class RealUserNetworkApi final : public IUserNetworkApi
{
    Q_OBJECT

public:
    explicit RealUserNetworkApi(BackendClient *backend, QObject *parent = nullptr);

    void loginByPhone(const QString &phone, const RequestContext &context) override;
    void queryCurrentUser(const QString &userId, const RequestContext &context) override;
    void updateNickname(const QString &userId,
                        const QString &nickname,
                        const RequestContext &context) override;
    void logout(const RequestContext &context) override;

    // 供测试与部署调节；默认 10 秒
    void setRequestTimeoutMs(int timeoutMs);

    enum class PendingKind
    {
        Login,
        QueryProfile,
        UpdateNickname
    };

private:
    struct PendingRequest
    {
        PendingKind kind = PendingKind::Login;
        QString requestId;
        QString operationId;
        QString userId;
        QTimer *timer = nullptr;
    };

    bool startRequest(PendingKind kind, const QJsonObject &payload,
                      const RequestContext &context, const QString &userId);
    void handleFrame(int msgType, const QJsonObject &payload);
    void handleServerError(int errType, const QJsonObject &payload);
    void handleTimeout(const QString &requestId);
    void failPending(PendingRequest pending, const ClientError &error);
    void failAllPending(const QString &code, const QString &message);
    bool takePending(const QString &requestId, PendingKind expectedKind,
                     PendingRequest *out);
    PendingRequest *findPendingByRequestId(const QString &requestId);
    PendingRequest *findPendingForResponseByRequestId(const QString &requestId);
    PendingRequest *findPendingForResponse(PendingKind kind,
                                           const QString &echoedRequestId);
    PendingRequest *findMostRecentPending();
    ClientError makeTimeoutError(PendingKind kind, const PendingRequest &pending) const;
    static AccountStatus parseStatus(const QString &status);
    static bool successPayloadValid(PendingKind kind, const QJsonObject &payload);

    BackendClient *m_backend;
    int m_requestTimeoutMs = 10000;
    QHash<QString, PendingRequest> m_pendingRequests;
    QList<QString> m_pendingOrder;   // 创建顺序，供 3xx 无回显时归属最近在途请求
};
