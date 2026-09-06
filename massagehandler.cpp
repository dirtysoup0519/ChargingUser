#include "massagehandler.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QtEndian>

/* ============ MassageFrame ============ */

QJsonObject MassageFrame::toJson() const
{
    if (msg.isEmpty())
        return QJsonObject();
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(msg, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject();
    return doc.object();
}

/* ============ 构造 ============ */

MassageHandler::MassageHandler(QObject *parent)
    : QObject{parent}
{
}

/* ============ 打包 ============ */

/* 头部写入工具：4B 类型（大端）+ 8B 长度（大端） */
static void writeHead(QByteArray &out, int msgType, qint64 msgSize)
{
    quint32 t = qToBigEndian<quint32>(static_cast<quint32>(msgType));
    quint64 s = qToBigEndian<quint64>(static_cast<quint64>(msgSize));
    out.append(reinterpret_cast<const char *>(&t), MSG_TYPE_LEN);
    out.append(reinterpret_cast<const char *>(&s), MSG_SIZE_LEN);
}

QByteArray MassageHandler::pack(int msgType, const QJsonObject &msg)
{
    QByteArray payload = QJsonDocument(msg).toJson(QJsonDocument::Compact);

    /* 载荷不超过分片阈值：单帧直接发 */
    if (payload.size() <= BIGDATA_THRESHOLD)
    {
        QByteArray frame;
        writeHead(frame, msgType, payload.size());
        frame.append(payload);
        return frame;
    }

    /* 大数据分片：START 载荷 = [4B 原始类型码][数据块0]，MID/END 为后续数据块 */
    QByteArray out;
    const int chunkSize = BIGDATA_THRESHOLD;

    QByteArray first;
    quint32 real = qToBigEndian<quint32>(static_cast<quint32>(msgType));
    first.append(reinterpret_cast<const char *>(&real), MSG_TYPE_LEN);
    first.append(payload.constData(), chunkSize);
    writeHead(out, BIGDATA_START, first.size());
    out.append(first);

    qint64 offset = chunkSize;
    while (offset + chunkSize < payload.size())
    {
        writeHead(out, BIGDATA_MID, chunkSize);
        out.append(payload.constData() + offset, chunkSize);
        offset += chunkSize;
    }

    writeHead(out, BIGDATA_END, payload.size() - offset);
    out.append(payload.constData() + offset, payload.size() - offset);
    return out;
}

QByteArray MassageHandler::pack(int msgType, const QJsonArray &arr)
{
    QJsonObject wrap;
    wrap.insert("data", arr);
    return pack(msgType, wrap);
}

QByteArray MassageHandler::packError(int errType, const QString &reason, const QString &bizCode)
{
    QJsonObject msg;
    if (!bizCode.isEmpty())
        msg.insert("code", bizCode);    // 业务错误码（BIZ_ERR_*），供客户端精确提示
    msg.insert("err", reason);
    return pack(errType, msg);
}

QJsonObject MassageHandler::fromPayload(const QByteArray &payload)
{
    if (payload.isEmpty())
        return QJsonObject();
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject();
    return doc.object();
}

/* ============ 业务消息构造 ============ */

QJsonObject MassageHandler::makeLogin(const QString &username,
                                      const QString &password,
                                      const QString &role)
{
    QJsonObject msg;
    msg.insert("username", username);
    msg.insert("password", password);
    msg.insert("role", role);
    return msg;
}

QJsonObject MassageHandler::makeOrderQuery(const QJsonObject &cond)
{
    return cond;    // 条件字段均可选：username / stationName / chargerCode / orderNo
}

QJsonObject MassageHandler::makeRecord(const QString &table, const QJsonObject &record)
{
    QJsonObject msg;
    msg.insert("table", table);
    msg.insert("record", record);
    return msg;
}

QJsonObject MassageHandler::makeUpdate(const QString &table,
                                       const QJsonObject &key,
                                       const QJsonObject &fields)
{
    QJsonObject msg;
    msg.insert("table", table);
    msg.insert("key", key);
    msg.insert("fields", fields);
    return msg;
}

QJsonObject MassageHandler::makeDelete(const QString &table, const QJsonObject &key)
{
    QJsonObject msg;
    msg.insert("table", table);
    msg.insert("key", key);
    return msg;
}

QJsonObject MassageHandler::makeChargingReport(const QString &chargerCode,
                                               const QString &stationName,
                                               const QString &username,
                                               double kwh)
{
    QJsonObject msg;
    msg.insert("chargerCode", chargerCode);
    msg.insert("stationName", stationName);
    msg.insert("username", username);
    msg.insert("kwh", kwh);
    return msg;
}

QJsonObject MassageHandler::makeDevOnline(const QString &chargerCode, const QString &stationName)
{
    QJsonObject msg;
    msg.insert("chargerCode", chargerCode);
    msg.insert("stationName", stationName);
    return msg;
}

QJsonObject MassageHandler::makeDevOffline(const QString &chargerCode)
{
    QJsonObject msg;
    msg.insert("chargerCode", chargerCode);
    return msg;
}

QByteArray MassageHandler::makeHeartbeat()
{
    QByteArray frame;
    writeHead(frame, HEARTBEAT, 0);     // 心跳帧无载荷
    return frame;
}

/* ============ 解析状态机 ============ */

void MassageHandler::reset()
{
    recvBuf.clear();
    resetAssembly();
}

void MassageHandler::resetAssembly()
{
    assemblingBuf.clear();
    assemblingType = 0;
    assembling = false;
}

void MassageHandler::feed(const QByteArray &data)
{
    recvBuf.append(data);
    tryParseFrames();
}

void MassageHandler::tryParseFrames()
{
    while (true)
    {
        /* 不足一个帧头：等待更多数据 */
        if (recvBuf.size() < FRAME_HEAD_LEN)
            return;

        quint32 typeBE;
        quint64 sizeBE;
        memcpy(&typeBE, recvBuf.constData(), MSG_TYPE_LEN);
        memcpy(&sizeBE, recvBuf.constData() + MSG_TYPE_LEN, MSG_SIZE_LEN);
        const int    msgType = static_cast<int>(qFromBigEndian<quint32>(typeBE));
        const qint64 msgSize = static_cast<qint64>(qFromBigEndian<quint64>(sizeBE));

        /* 非法长度：丢弃缓冲，防止错误码流导致内存膨胀 */
        if (msgSize < 0 || msgSize > MAX_MSG_SIZE)
        {
            // 无法从非法长度可靠定位下一帧边界；同时丢弃半包与分片状态，
            // 防止旧 START 在后续 END 到达时被错误交付。
            reset();
            emit frameReady(ILLEGAL_REQUEST, QByteArray("{\"err\":\"bad frame size\"}"));
            return;
        }

        /* 载荷未到齐：半包，缓存等待 */
        if (recvBuf.size() < FRAME_HEAD_LEN + static_cast<int>(msgSize))
            return;

        QByteArray payload = recvBuf.mid(FRAME_HEAD_LEN, static_cast<int>(msgSize));
        recvBuf.remove(0, FRAME_HEAD_LEN + static_cast<int>(msgSize));

        dispatchChunk(msgType, payload);    // 分片帧在此重组，完整帧经 deliverFrame 交付
    }
}

void MassageHandler::dispatchChunk(int msgType, const QByteArray &payload)
{
    /* 分片重组：START 记录原始类型码并开缓冲，MID 追加，END 收尾交付 */
    if (msgType == BIGDATA_START)
    {
        // 新 START 总是替换旧的未完成分片，避免两条消息发生拼接。
        resetAssembly();
        if (payload.size() < MSG_TYPE_LEN) {
            rejectChunk("bad chunk start");
            return;
        }
        quint32 realBE;
        memcpy(&realBE, payload.constData(), MSG_TYPE_LEN);
        assemblingType = static_cast<int>(qFromBigEndian<quint32>(realBE));
        assemblingBuf  = payload.mid(MSG_TYPE_LEN);
        assembling     = true;
        return;
    }
    if (msgType == BIGDATA_MID)
    {
        if (!assembling) {
            rejectChunk("unexpected chunk middle");
            return;
        }
        assemblingBuf.append(payload);
        return;
    }
    if (msgType == BIGDATA_END)
    {
        if (!assembling) {
            rejectChunk("unexpected chunk end");
            return;
        }
        assemblingBuf.append(payload);
        const QByteArray full = assemblingBuf;
        const int realType = assemblingType;
        resetAssembly();
        deliverFrame(realType, full);
        return;
    }

    /* 普通帧直接交付 */
    deliverFrame(msgType, payload);
}

void MassageHandler::deliverFrame(int msgType, const QByteArray &payload)
{
    emit frameReady(msgType, payload);
}

void MassageHandler::rejectChunk(const char *reason)
{
    resetAssembly();
    QJsonObject error;
    error.insert(QStringLiteral("err"), QString::fromLatin1(reason));
    emit frameReady(ILLEGAL_REQUEST,
                    QJsonDocument(error).toJson(QJsonDocument::Compact));
}
