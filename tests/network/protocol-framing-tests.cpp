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

QTEST_GUILESS_MAIN(ProtocolFramingTests)

#include "protocol-framing-tests.moc"
