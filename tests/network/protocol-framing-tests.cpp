#include "massagehandler.h"
#include "protocol.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtEndian>
#include <QtTest>

#include <limits>

namespace
{

QByteArray rawFrame(int msgType, quint64 declaredSize, const QByteArray &payload = QByteArray())
{
    QByteArray frame;
    const quint32 type = qToBigEndian<quint32>(static_cast<quint32>(msgType));
    const quint64 size = qToBigEndian<quint64>(declaredSize);
    frame.append(reinterpret_cast<const char *>(&type), MSG_TYPE_LEN);
    frame.append(reinterpret_cast<const char *>(&size), MSG_SIZE_LEN);
    frame.append(payload);
    return frame;
}

QByteArray chunkStart(int originalType, const QByteArray &payload)
{
    const quint32 type = qToBigEndian<quint32>(static_cast<quint32>(originalType));
    QByteArray startPayload(reinterpret_cast<const char *>(&type), MSG_TYPE_LEN);
    startPayload.append(payload);
    return rawFrame(BIGDATA_START, static_cast<quint64>(startPayload.size()), startPayload);
}

} // namespace

class ProtocolFramingTests final : public QObject
{
    Q_OBJECT

private slots:
    void completeFrameRoundTrip();
    void splitAndStickyFramesAreParsedInOrder();
    void largePayloadIsChunkedAndReassembled();
    void oversizedFrameResetsPartialAssembly();
    void negativeEncodedLengthIsRejected();
    void malformedStartClearsPreviousAssembly();
    void unexpectedChunksDoNotPolluteFollowingFrame();
    void resetClearsPartialHeader();
    void resetClearsPartialAssembly();
    void unknownMessageTypeDoesNotPolluteFollowingFrame();
    /* 调试指南 §9 修复项 4：分片资源边界回归 */
    void oversizedChunkSingleBlockIsRejected();
    void chunkedMessageTotalOverLimitIsRejectedOnce();
    void assemblyRecoversAfterOverLimit();
    void packRejectsPayloadBeyondAssembledLimit();
};

void ProtocolFramingTests::completeFrameRoundTrip()
{
    MassageHandler handler;
    QSignalSpy frames(&handler, &MassageHandler::frameReady);
    QJsonObject payload;
    payload.insert(QStringLiteral("value"), 42);

    handler.feed(MassageHandler::pack(DATA, payload));

    QCOMPARE(frames.count(), 1);
    QCOMPARE(frames.at(0).at(0).toInt(), DATA);
    QCOMPARE(MassageHandler::fromPayload(frames.at(0).at(1).toByteArray()), payload);
}

void ProtocolFramingTests::splitAndStickyFramesAreParsedInOrder()
{
    MassageHandler handler;
    QSignalSpy frames(&handler, &MassageHandler::frameReady);
    const QByteArray first = MassageHandler::makeHeartbeat();
    const QByteArray second = MassageHandler::pack(HEARTBEAT_ACK);

    handler.feed(first.left(3));
    QCOMPARE(frames.count(), 0);
    handler.feed(first.mid(3) + second);

    QCOMPARE(frames.count(), 2);
    QCOMPARE(frames.at(0).at(0).toInt(), HEARTBEAT);
    QCOMPARE(frames.at(0).at(1).toByteArray().size(), 0);
    QCOMPARE(frames.at(1).at(0).toInt(), HEARTBEAT_ACK);
}

void ProtocolFramingTests::largePayloadIsChunkedAndReassembled()
{
    MassageHandler handler;
    QSignalSpy frames(&handler, &MassageHandler::frameReady);
    QJsonObject object;
    object.insert(QStringLiteral("blob"),
                  QString(BIGDATA_THRESHOLD * 2 + 137, QLatin1Char('x')));
    const QByteArray expected = QJsonDocument(object).toJson(QJsonDocument::Compact);
    const QByteArray encoded = MassageHandler::pack(DATA, object);

    QVERIFY(encoded.size() > expected.size() + FRAME_HEAD_LEN);
    for (qsizetype offset = 0; offset < encoded.size(); offset += 997) {
        handler.feed(encoded.mid(offset, 997));
    }

    QCOMPARE(frames.count(), 1);
    QCOMPARE(frames.at(0).at(0).toInt(), DATA);
    QCOMPARE(frames.at(0).at(1).toByteArray(), expected);
}

void ProtocolFramingTests::oversizedFrameResetsPartialAssembly()
{
    MassageHandler handler;
    QSignalSpy frames(&handler, &MassageHandler::frameReady);

    handler.feed(chunkStart(DATA, QByteArrayLiteral("stale")));
    handler.feed(rawFrame(DATA, static_cast<quint64>(MAX_MSG_SIZE) + 1));
    handler.feed(rawFrame(BIGDATA_END, 4, QByteArrayLiteral("tail")));
    handler.feed(MassageHandler::makeHeartbeat());

    QCOMPARE(frames.count(), 3);
    QCOMPARE(frames.at(0).at(0).toInt(), ILLEGAL_REQUEST);
    QCOMPARE(frames.at(1).at(0).toInt(), ILLEGAL_REQUEST);
    QCOMPARE(frames.at(2).at(0).toInt(), HEARTBEAT);
}

void ProtocolFramingTests::negativeEncodedLengthIsRejected()
{
    MassageHandler handler;
    QSignalSpy frames(&handler, &MassageHandler::frameReady);

    handler.feed(rawFrame(DATA, std::numeric_limits<quint64>::max()));
    handler.feed(MassageHandler::makeHeartbeat());

    QCOMPARE(frames.count(), 2);
    QCOMPARE(frames.at(0).at(0).toInt(), ILLEGAL_REQUEST);
    QCOMPARE(frames.at(1).at(0).toInt(), HEARTBEAT);
}

void ProtocolFramingTests::malformedStartClearsPreviousAssembly()
{
    MassageHandler handler;
    QSignalSpy frames(&handler, &MassageHandler::frameReady);

    handler.feed(chunkStart(DATA, QByteArrayLiteral("stale")));
    handler.feed(rawFrame(BIGDATA_START, 3, QByteArrayLiteral("bad")));
    handler.feed(rawFrame(BIGDATA_END, 4, QByteArrayLiteral("tail")));
    handler.feed(MassageHandler::makeHeartbeat());

    QCOMPARE(frames.count(), 3);
    QCOMPARE(frames.at(0).at(0).toInt(), ILLEGAL_REQUEST);
    QCOMPARE(frames.at(1).at(0).toInt(), ILLEGAL_REQUEST);
    QCOMPARE(frames.at(2).at(0).toInt(), HEARTBEAT);
}

void ProtocolFramingTests::unexpectedChunksDoNotPolluteFollowingFrame()
{
    MassageHandler handler;
    QSignalSpy frames(&handler, &MassageHandler::frameReady);
    const QByteArray unexpected = rawFrame(BIGDATA_MID, 3, QByteArrayLiteral("bad"));

    handler.feed(unexpected + MassageHandler::makeHeartbeat());

    QCOMPARE(frames.count(), 2);
    QCOMPARE(frames.at(0).at(0).toInt(), ILLEGAL_REQUEST);
    QCOMPARE(frames.at(1).at(0).toInt(), HEARTBEAT);
}

void ProtocolFramingTests::resetClearsPartialHeader()
{
    MassageHandler handler;
    QSignalSpy frames(&handler, &MassageHandler::frameReady);
    const QByteArray frame = MassageHandler::makeHeartbeat();

    handler.feed(frame.left(7));
    handler.reset();
    handler.feed(frame);

    QCOMPARE(frames.count(), 1);
    QCOMPARE(frames.at(0).at(0).toInt(), HEARTBEAT);
}

void ProtocolFramingTests::resetClearsPartialAssembly()
{
    MassageHandler handler;
    QSignalSpy frames(&handler, &MassageHandler::frameReady);

    handler.feed(chunkStart(DATA, QByteArrayLiteral("stale")));
    handler.reset();
    handler.feed(rawFrame(BIGDATA_END, 4, QByteArrayLiteral("tail"))
                 + MassageHandler::makeHeartbeat());

    QCOMPARE(frames.count(), 2);
    QCOMPARE(frames.at(0).at(0).toInt(), ILLEGAL_REQUEST);
    QCOMPARE(frames.at(1).at(0).toInt(), HEARTBEAT);
}

void ProtocolFramingTests::unknownMessageTypeDoesNotPolluteFollowingFrame()
{
    MassageHandler handler;
    QSignalSpy frames(&handler, &MassageHandler::frameReady);

    handler.feed(rawFrame(999, 0) + MassageHandler::makeHeartbeat());

    QCOMPARE(frames.count(), 2);
    QCOMPARE(frames.at(0).at(0).toInt(), 999);
    QCOMPARE(frames.at(1).at(0).toInt(), HEARTBEAT);
}

/* ---------- 以下为调试指南 §9 修复项 4 的分片资源边界回归 ---------- */

/* 修复项 2：单块超过各自上限（START ≤ 4+16KB，MID/END ≤ 16KB）应被拒绝，
 * 且拒绝后后续正常帧不受污染 */
void ProtocolFramingTests::oversizedChunkSingleBlockIsRejected()
{
    MassageHandler handler;
    QSignalSpy frames(&handler, &MassageHandler::frameReady);

    // 合法 START（数据块0 恰为上限 16KB）
    handler.feed(chunkStart(DATA, QByteArray(BIGDATA_THRESHOLD, 'a')));
    QCOMPARE(frames.count(), 0);

    // MID 超限 1 字节：必须上报 ILLEGAL_REQUEST 且不追加进重组缓冲
    handler.feed(rawFrame(BIGDATA_MID,
                          static_cast<quint64>(BIGDATA_THRESHOLD) + 1,
                          QByteArray(BIGDATA_THRESHOLD + 1, 'b')));
    QCOMPARE(frames.count(), 1);
    QCOMPARE(frames.at(0).at(0).toInt(), ILLEGAL_REQUEST);

    // 拒绝后心跳不受影响（分片状态已清理）
    handler.feed(MassageHandler::makeHeartbeat());
    QCOMPARE(frames.count(), 2);
    QCOMPARE(frames.at(1).at(0).toInt(), HEARTBEAT);

    // END 超限同样拒绝（先重新建立合法 START）
    handler.feed(chunkStart(DATA, QByteArray(BIGDATA_THRESHOLD, 'c')));
    QCOMPARE(frames.count(), 2);
    handler.feed(rawFrame(BIGDATA_END,
                          static_cast<quint64>(BIGDATA_THRESHOLD) + 1,
                          QByteArray(BIGDATA_THRESHOLD + 1, 'd')));
    QCOMPARE(frames.count(), 3);
    QCOMPARE(frames.at(2).at(0).toInt(), ILLEGAL_REQUEST);
}

/* 修复项 3：每块都合法但累计越过 8MB 上限时，必须恰好上报一次协议错误；
 * 同一条毒消息的剩余分片（含 END）静默吞掉，不得逐块刷错误 */
void ProtocolFramingTests::chunkedMessageTotalOverLimitIsRejectedOnce()
{
    MassageHandler handler;
    QSignalSpy frames(&handler, &MassageHandler::frameReady);

    // START 数据块0 = 16KB，随后每个 MID 恰为 16KB；
    // 累计 (1+N)*16KB，N=512 时首次越过 8MB 上限 → 恰在第 512 个 MID 触发
    handler.feed(chunkStart(DATA, QByteArray(BIGDATA_THRESHOLD, 'a')));
    const QByteArray mid = rawFrame(BIGDATA_MID,
                                    static_cast<quint64>(BIGDATA_THRESHOLD),
                                    QByteArray(BIGDATA_THRESHOLD, 'm'));
    for (int i = 0; i < MAX_ASSEMBLED_MSG_SIZE / BIGDATA_THRESHOLD; ++i)
        handler.feed(mid);

    QCOMPARE(frames.count(), 1);                       // 只报一次
    QCOMPARE(frames.at(0).at(0).toInt(), ILLEGAL_REQUEST);

    // 毒消息的 END 到达：丢弃态下静默吞掉，不产生第二条错误
    handler.feed(rawFrame(BIGDATA_END, 4, QByteArrayLiteral("tail")));
    QCOMPARE(frames.count(), 1);
}

/* 修复项 3 后半：超限拒绝后，下一条合法分片消息必须能完整重组交付 */
void ProtocolFramingTests::assemblyRecoversAfterOverLimit()
{
    MassageHandler handler;
    QSignalSpy frames(&handler, &MassageHandler::frameReady);

    // 先制造一次累计超限
    handler.feed(chunkStart(DATA, QByteArray(BIGDATA_THRESHOLD, 'a')));
    const QByteArray mid = rawFrame(BIGDATA_MID,
                                    static_cast<quint64>(BIGDATA_THRESHOLD),
                                    QByteArray(BIGDATA_THRESHOLD, 'm'));
    for (int i = 0; i < MAX_ASSEMBLED_MSG_SIZE / BIGDATA_THRESHOLD; ++i)
        handler.feed(mid);
    QCOMPARE(frames.count(), 1);

    // 随后一条合法的大消息（约 2 倍阈值，走 400/401/402 分片）正常送达
    QJsonObject object;
    object.insert(QStringLiteral("blob"),
                  QString(BIGDATA_THRESHOLD * 2 + 33, QLatin1Char('x')));
    const QByteArray expected = QJsonDocument(object).toJson(QJsonDocument::Compact);
    handler.feed(MassageHandler::pack(DATA, object));

    QCOMPARE(frames.count(), 2);
    QCOMPARE(frames.at(1).at(0).toInt(), DATA);
    QCOMPARE(frames.at(1).at(1).toByteArray(), expected);
}

/* 修复项 5：pack() 对超过完整消息上限的载荷返回空 QByteArray；
 * 上限以内的载荷仍允许打包（边界不误伤）。
 * 注意 JSON 包装有约 10 字节开销（{"blob":""}），边界值须预留余量 */
void ProtocolFramingTests::packRejectsPayloadBeyondAssembledLimit()
{
    // JSON 序列化后必然越过 8MB 上限 → 拒绝打包
    QJsonObject tooBig;
    tooBig.insert(QStringLiteral("blob"),
                  QString(MAX_ASSEMBLED_MSG_SIZE, QLatin1Char('x')));
    QVERIFY(MassageHandler::pack(DATA, tooBig).isEmpty());

    // 留出 JSON 包装余量后接近上限：应成功分片打包
    QJsonObject nearLimit;
    nearLimit.insert(QStringLiteral("blob"),
                     QString(MAX_ASSEMBLED_MSG_SIZE - 64, QLatin1Char('x')));
    const QByteArray packed = MassageHandler::pack(DATA, nearLimit);
    QVERIFY(!packed.isEmpty());
    QVERIFY(packed.size() > MAX_ASSEMBLED_MSG_SIZE - 64);  // 分片流必然大于载荷本身
}

QTEST_GUILESS_MAIN(ProtocolFramingTests)

#include "protocol-framing-tests.moc"
