/* 消息处理系统 客户端 服务端可复用
 * （即需求中的 MessageManager，工程内沿用 MassageHandler 命名）
 *
 * 职责：
 *   1. 报文打包：业务 JSON -> 协议帧（超长自动按 BIGDATA_* 分片）
 *   2. 报文解析：原始字节流 -> 完整业务帧（粘包/半包/分片重组，全部在本类消化）
 *   3. 业务消息构造辅助：登录、心跳、错误应答等常用消息
 * 本类不依赖网络类，客户端与服务器端共同遵循，直接拷贝本文件组到两端工程。
 */

#ifndef MASSAGEHANDLER_H
#define MASSAGEHANDLER_H

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QByteArray>
#include <QList>

#include "protocol.h"

/* 一条完整业务消息（分片重组后交付的就是它） */
struct MassageFrame
{
    int        msgType  = 0;    // 消息类型码
    qint64     msgSize  = 0;    // msg 字段长度
    QByteArray msg;             // msg 内容（UTF-8 JSON 文本）

    /* 便捷解析：把 msg 内容反序列化为 JSON 对象，失败返回空对象 */
    QJsonObject toJson() const;
};

class MassageHandler : public QObject
{
    Q_OBJECT
public:
    explicit MassageHandler(QObject *parent = nullptr);

    /* ---------- 打包（客户端构造请求 / 服务端构造应答均可用） ---------- */

    // 业务消息 -> 完整协议帧（可含分片）；msg 为空则发空载荷帧。
    // 返回合同（调试指南 §9 修复项 5）：载荷超过 MAX_ASSEMBLED_MSG_SIZE 时
    // 返回空 QByteArray 表示拒绝打包——这样的消息对端解析器必然拒绝，
    // 提前拦截可避免发送无效流量；调用方发送前应检查空返回。
    static QByteArray pack(int msgType, const QJsonObject &msg = QJsonObject());

    // 查询结果（JSON 数组）打包，内部转成 {"data":[...]} 再走通用打包
    static QByteArray pack(int msgType, const QJsonArray &arr);

    // 错误应答帧，如 packError(DATA_NOEXIST, "电桩不存在")；bizCode 为业务错误码（可空）
    static QByteArray packError(int errType, const QString &reason,
                                const QString &bizCode = QString());

    // 载荷 -> JSON 对象（空载荷/解析失败返回空对象）
    static QJsonObject fromPayload(const QByteArray &payload);

    /* ---------- 常用业务消息构造 ---------- */
    static QJsonObject makeLogin(const QString &username,
                                 const QString &password,
                                 const QString &role);            // LOGIN_REQ
    static QJsonObject makeOrderQuery(const QJsonObject &cond);   // ORDERQRY_REQ
    static QJsonObject makeRecord(const QString &table,
                                  const QJsonObject &record);     // ADDDATA
    static QJsonObject makeUpdate(const QString &table,
                                  const QJsonObject &key,
                                  const QJsonObject &fields);     // UPDDATA
    static QJsonObject makeDelete(const QString &table,
                                  const QJsonObject &key);        // DELDATA
    static QJsonObject makeChargingReport(const QString &chargerCode,
                                          const QString &stationName,
                                          const QString &username,
                                          double kwh);            // CHGDATA_REQ
    static QJsonObject makeDevOnline(const QString &chargerCode,
                                     const QString &stationName); // DEV_ONLINE
    static QJsonObject makeDevOffline(const QString &chargerCode);   // DEV_OFFLINE
    static QByteArray makeHeartbeat();                            // HEARTBEAT 帧

    /* ---------- 解析（有状态：粘包/半包缓冲 + 分片重组） ---------- */

    // 喂入 socket 收到的原始字节；每解析出一条完整业务帧发一次 frameReady
    void feed(const QByteArray &data);

    // 重建解析状态（断线重连、socket 复用前调用，丢弃残留半包）
    void reset();

signals:
    // 解析出一条完整业务消息（分片已重组）
    void frameReady(int msgType, const QByteArray &payload);

private:
    QByteArray recvBuf;             // 半包缓冲
    QByteArray assemblingBuf;       // 大数据分片重组缓冲
    int        assemblingType = 0;  // 被重组帧的原始业务类型码
    bool       assembling     = false;
    /* 丢弃态（调试指南 §9 修复项 3）：累计超限拒绝后置位，同一条毒消息的
     * 剩余 MID/END 被静默吞掉，保证"超限只上报一次协议错误"；新 START 复位。 */
    bool       droppingAssembly = false;

    void tryParseFrames();          // 从 recvBuf 循环取帧
    void deliverFrame(int msgType, const QByteArray &payload);
    void dispatchChunk(int msgType, const QByteArray &payload); // 分片重组
    void resetAssembly();
    void rejectChunk(const char *reason);
};

#endif // MASSAGEHANDLER_H
