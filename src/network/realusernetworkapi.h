#pragma once

#include "modules/user/iusernetworkapi.h"

#include <QHash>
#include <QJsonObject>
#include <QList>

class BackendClient;
class QTimer;

/* IUserNetworkApi 的真实适配器（服务端协议 v2.6.3）：
 *  - 手机号登录与退出翻译为 116/102，经 BackendClient 收发；
 *  - 用户资料查询使用已登录用户可调用的 100/200，昵称修改使用 118/228；
 *  - 请求关联按合同 §11 v1.1/v1.2：优先用服务端回显的 requestId 匹配；
 *    成功应答无回显时回退到“该应答类型对应的最早在途请求”（同类防重保证无歧义）；
 *    错误应答无回显时仅在全局恰好一个在途请求时才归属（v1.2：避免跨类型误归属）；
 *  - 已发送请求的超时/断连保持保守失败语义；
 *  - 失败路径（断线/发送失败）必须携带 requestId，否则上层无法释放对应在途请求；
 *  - 退出登录等待 202 确认；本地会话仍由 UserService 立即清理。
 * 本类不维护会话世代——迟到响应过滤由 UserService 按 requestId/世代完成。
 */
class RealUserNetworkApi final : public IUserNetworkApi
{
    Q_OBJECT

public:
    explicit RealUserNetworkApi(BackendClient *backend, QObject *parent = nullptr);

    void loginByPhone(const QString &phone, const RequestContext &context) override;
    void loginByCredentials(const QString &username, const QString &password,
                            const RequestContext &context) override;
    void queryCurrentUser(const QString &userId, const RequestContext &context) override;
    void updateNickname(const QString &userId,
                        const QString &nickname,
                        const RequestContext &context) override;
    void changePassword(const QString &userId,
                        const QString &oldPassword,
                        const QString &newPassword,
                        const RequestContext &context) override;
    void logout(const RequestContext &context) override;

    // 供测试与部署调节；默认 10 秒
    void setRequestTimeoutMs(int timeoutMs);

    enum class PendingKind
    {
        Login,
        CredentialLogin,
        QueryProfile,
        UpdateNickname,
        UpdatePassword,
        Logout
    };

private:
    struct PendingRequest
    {
        PendingKind kind = PendingKind::Login;
        QString requestId;
        QString operationId;
        QString userId;
        QString requestedNickname;
        QTimer *timer = nullptr;
    };

    bool startRequest(PendingKind kind, const QJsonObject &payload,
                      const RequestContext &context, const QString &userId,
                      const QString &requestedNickname = QString());
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
    PendingRequest *findSolePending();
    ClientError makeTimeoutError(PendingKind kind, const PendingRequest &pending) const;
    static AccountStatus parseStatus(const QString &status);
    static bool successPayloadValid(PendingKind kind, const QJsonObject &payload,
                                    const QString &requestedUserId);

    BackendClient *m_backend;
    int m_requestTimeoutMs = 10000;
    QHash<QString, PendingRequest> m_pendingRequests;
    QList<QString> m_pendingOrder;   // 创建顺序，供成功应答无回显时按类型回退匹配
};
