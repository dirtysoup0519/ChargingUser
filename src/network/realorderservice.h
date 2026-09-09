#pragma once

#include "common/clienterror.h"
#include "common/connectionstate.h"
#include "common/requestcontext.h"
#include "modules/order/iorderservice.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>

#include <optional>

class BackendClient;
class QTimer;

/**
 * IOrderService 的真实适配器（服务端协议 v2.6，ORDERQRY 通道）。
 *
 * 协议现实约束：
 * - 订单查询走 `106 ORDERQRY_REQ {cond}` → `214 ORDERQRY_ACK {orders:[...]}`；
 * - 214 不回显 requestId、无表名，并发查询应答不可区分 → 全局同时只允许
 *   一个在途查询（与 RealChargerService 同款保守纪律），请求载荷仍携带
 *   requestId 以便服务端未来支持回显时自动升级；
 * - 查询条件固定以 username 收敛到当前用户（服务端 orderInfo 表权威键），
 *   活动订单 = status 为 Charging / PendingSettlement 的第一条。
 *
 * 字段映射（服务端权威键 orderNo）：
 * - orderId = orderNo（v2.5 语义，稳定性限制见 ordertypes.h 注释）；
 * - 金额/电量宽容解析：amountCents 优先，缺失时 amount(元) 四舍五入转分；
 * - 时间戳接受 ISO 与 "yyyy-MM-dd hh:mm:ss" 两种格式，解析失败按缺失处理
 *   （endedAt/paymentDeadline 本就是 optional），联调后按实际格式收敛。
 *
 * 停止请求使用 109/209；因协议要求 chargerCode，停止前先按 orderNo 查询订单。
 */
class RealOrderService final : public IOrderService
{
    Q_OBJECT

public:
    explicit RealOrderService(BackendClient *backend, QObject *parent = nullptr);

    /** 登录成功后注入当前用户身份（orderInfo.username）；登出时清空。 */
    void setIdentity(const QString &username);
    void setRequestTimeoutMs(int timeoutMs);

public slots:
    void queryActiveOrder(const RequestContext &context) override;
    void queryActiveOrders(const RequestContext &context) override;
    void queryOrderHistory(const RequestContext &context) override;
    void queryOrderDetail(const RequestContext &context,
                          const QString &orderId) override;
    void stopCharging(const RequestContext &context,
                      const QString &orderId) override;
    void queryStopResult(const RequestContext &context,
                         const QString &operationId) override;
    void cancel(const QString &requestId) override;

private slots:
    void handleFrame(int msgType, const QJsonObject &payload);
    void handleConnectionStateChanged(ConnectionState state);
    void handleTimeout();

private:
    enum class QueryKind
    {
        ActiveOrder,
        ActiveOrders,
        OrderHistory,
        OrderDetail,
        StopOrderLookup,
        StopRequest,
        StopResult
    };

    struct PendingRequest
    {
        QueryKind kind = QueryKind::ActiveOrder;
        QString requestId;
        QString operationId;
        QString orderId;
        QString chargerCode;
        QTimer *timer = nullptr;
    };

    struct QueuedQuery
    {
        QueryKind kind = QueryKind::ActiveOrder;
        RequestContext context;
        QString orderId;
    };

    bool startQuery(QueryKind kind, const RequestContext &context,
                    const QString &orderId);
    void startNextQueuedQuery();
    void finishPending();
    void failPending(const QString &code, const QString &message,
                     bool retryable, bool resultUnknown = false);
    void failAllPending(const QString &code, const QString &message);
    void emitFailed(const RequestContext &context, const QString &code,
                    const QString &message, bool retryable);

    static QJsonObject makeOrderQuery(const QJsonObject &cond,
                                      const QString &requestId);
    static ChargingOrder parseOrderRecord(const QJsonObject &record);

    BackendClient *m_backend;
    QString m_username;
    int m_requestTimeoutMs = 10000;
    /** 全局单在途查询：214 无 requestId 回显，并发应答无法区分归属。 */
    std::optional<PendingRequest> m_pending;
    QList<QueuedQuery> m_queryQueue;
};
