#include "massagehandler.h"
#include "network/qtnetworktransport.h"
#include "protocol.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextStream>
#include <QTimer>
#include <QUuid>

namespace
{

enum ExitCode
{
    Success = 0,
    InvalidArguments = 2,
    TransportFailure = 3,
    TimedOut = 4,
    ProtocolFailure = 5,
    SendFailure = 6
};

class NetworkSmokeRunner final : public QObject
{
    Q_OBJECT

public:
    NetworkSmokeRunner(const QString &host, quint16 port, int timeoutMs,
                       const QString &phone, QObject *parent = nullptr)
        : QObject(parent)
        , m_transport(host, port, this)
        , m_handler(this)
        , m_timeoutMs(timeoutMs)
        , m_phone(phone)
        , m_hostForLog(host)
        , m_portForLog(port)
    {
        m_timeoutTimer.setSingleShot(true);

        connect(&m_transport, &INetworkTransport::connected,
                this, &NetworkSmokeRunner::handleConnected);
        connect(&m_transport, &INetworkTransport::disconnected,
                this, &NetworkSmokeRunner::handleDisconnected);
        connect(&m_transport, &INetworkTransport::transportError,
                this, &NetworkSmokeRunner::handleTransportError);
        connect(&m_transport, &INetworkTransport::dataReceived,
                &m_handler, &MassageHandler::feed);
        connect(&m_handler, &MassageHandler::frameReady,
                this, &NetworkSmokeRunner::handleFrame);
        connect(&m_timeoutTimer, &QTimer::timeout, this, [this] {
            fail(TimedOut,
                 QStringLiteral("Timed out after %1 ms while %2.")
                     .arg(m_timeoutMs)
                     .arg(stateDescription()));
        });
    }

    void start()
    {
        qInfo().noquote() << QStringLiteral("Connecting to %1:%2 (timeout %3 ms)...")
                                 .arg(m_hostForLog)
                                 .arg(m_portForLog)
                                 .arg(m_timeoutMs);
        if (!m_phone.isEmpty()) {
            qWarning().noquote()
                << QStringLiteral("Phone login enabled for %1; an unknown number may be auto-registered.")
                       .arg(maskPhone(m_phone));
        }
        m_timeoutTimer.start(m_timeoutMs);
        m_transport.connectToServer();
    }

private:
    enum class State
    {
        Connecting,
        WaitingHeartbeat,
        WaitingPhoneLogin
    };

    static QString maskPhone(const QString &phone)
    {
        if (phone.size() < 7) {
            return QStringLiteral("<redacted>");
        }
        return phone.left(3) + QStringLiteral("****") + phone.right(4);
    }

    QString sanitizeReason(QString reason) const
    {
        if (!m_phone.isEmpty()) {
            reason.replace(m_phone, QStringLiteral("<phone>"));
        }
        reason.replace(QLatin1Char('\r'), QLatin1Char(' '));
        reason.replace(QLatin1Char('\n'), QLatin1Char(' '));
        return reason.left(200);
    }

    QString stateDescription() const
    {
        switch (m_state) {
        case State::Connecting:
            return QStringLiteral("connecting");
        case State::WaitingHeartbeat:
            return QStringLiteral("waiting for HEARTBEAT_ACK (230)");
        case State::WaitingPhoneLogin:
            return QStringLiteral("waiting for PHONE_LOGIN_ACK (217)");
        }
        return QStringLiteral("waiting for server response");
    }

    void handleConnected()
    {
        if (m_finished) {
            return;
        }
        qInfo() << "Connected.";
        m_state = State::WaitingHeartbeat;
        if (!m_transport.send(MassageHandler::makeHeartbeat())) {
            fail(SendFailure, QStringLiteral("Failed to queue HEARTBEAT_REQ (107)."));
            return;
        }
        qInfo() << "Sent message type=107 payloadBytes=0.";
    }

    void handleDisconnected()
    {
        if (!m_finished) {
            fail(TransportFailure,
                 QStringLiteral("Connection closed while %1.").arg(stateDescription()));
        }
    }

    void handleTransportError(const QString &message)
    {
        if (!m_finished) {
            fail(TransportFailure,
                 QStringLiteral("Transport error: %1").arg(sanitizeReason(message)));
        }
    }

    void handleFrame(int msgType, const QByteArray &payload)
    {
        if (m_finished) {
            return;
        }

        qInfo().noquote() << QStringLiteral("Received message type=%1 payloadBytes=%2.")
                                 .arg(msgType)
                                 .arg(payload.size());

        if (msgType >= DATA_NOEXIST && msgType <= PARAM_ERROR) {
            const QJsonObject errorPayload = MassageHandler::fromPayload(payload);
            QString reason = errorPayload.value(QStringLiteral("err")).toString();
            if (reason.isEmpty()) {
                reason = errorPayload.value(QStringLiteral("reason")).toString();
            }
            const QString code = errorPayload.value(QStringLiteral("code")).toString();
            fail(ProtocolFailure,
                 QStringLiteral("Server error type=%1 code=%2 reason=%3")
                     .arg(msgType)
                     .arg(code.isEmpty() ? QStringLiteral("<none>")
                                         : sanitizeReason(code))
                     .arg(reason.isEmpty() ? QStringLiteral("<none>")
                                           : sanitizeReason(reason)));
            return;
        }

        if (m_state == State::WaitingHeartbeat && msgType == HEARTBEAT_ACK) {
            if (m_phone.isEmpty()) {
                succeed(QStringLiteral("Heartbeat round trip completed."));
                return;
            }

            QJsonObject request;
            request.insert(QStringLiteral("phone"), m_phone);
            request.insert(QStringLiteral("requestId"),
                           QUuid::createUuid().toString(QUuid::WithoutBraces));
            const QByteArray loginFrame = MassageHandler::pack(PHONE_LOGIN_REQ, request);
            m_state = State::WaitingPhoneLogin;
            if (!m_transport.send(loginFrame)) {
                fail(SendFailure,
                     QStringLiteral("Failed to queue PHONE_LOGIN_REQ (116)."));
                return;
            }
            qInfo().noquote()
                << QStringLiteral("Sent message type=116 payloadBytes=%1 for %2; payload not logged.")
                       .arg(loginFrame.size() - FRAME_HEAD_LEN)
                       .arg(maskPhone(m_phone));
            return;
        }

        if (m_state == State::WaitingPhoneLogin && msgType == PHONE_LOGIN_ACK) {
            const QJsonObject response = MassageHandler::fromPayload(payload);
            if (response.value(QStringLiteral("username")).toString().isEmpty()) {
                fail(ProtocolFailure,
                     QStringLiteral("PHONE_LOGIN_ACK (217) is missing username."));
                return;
            }
            succeed(QStringLiteral("Heartbeat and phone-login round trips completed."));
            return;
        }

        qInfo() << "Message is not the response currently awaited; continuing to wait.";
    }

    void succeed(const QString &message)
    {
        finish(Success, message, false);
    }

    void fail(int exitCode, const QString &message)
    {
        finish(exitCode, message, true);
    }

    void finish(int exitCode, const QString &message, bool error)
    {
        if (m_finished) {
            return;
        }
        m_finished = true;
        m_timeoutTimer.stop();
        if (error) {
            qCritical().noquote() << message;
        } else {
            qInfo().noquote() << message;
        }
        m_transport.disconnectFromServer();
        QCoreApplication::exit(exitCode);
    }

    QtNetworkTransport m_transport;
    MassageHandler m_handler;
    QTimer m_timeoutTimer;
    int m_timeoutMs;
    QString m_phone;
    QString m_hostForLog;
    quint16 m_portForLog = 0;
    State m_state = State::Connecting;
    bool m_finished = false;
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("network-smoke"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("TCP framing and heartbeat smoke test. The default heartbeat-only mode does not modify business data."));
    const QCommandLineOption helpOption = parser.addHelpOption();
    const QCommandLineOption versionOption = parser.addVersionOption();
    const QCommandLineOption hostOption(
        {QStringLiteral("H"), QStringLiteral("host")},
        QStringLiteral("Server host or IP address."), QStringLiteral("host"),
        QString::fromLatin1(SERVER_IP));
    const QCommandLineOption portOption(
        {QStringLiteral("p"), QStringLiteral("port")},
        QStringLiteral("Server TCP port."), QStringLiteral("port"),
        QString::fromLatin1(SERVER_PORT));
    const QCommandLineOption timeoutOption(
        {QStringLiteral("t"), QStringLiteral("timeout")},
        QStringLiteral("Total run timeout in milliseconds."), QStringLiteral("ms"),
        QStringLiteral("10000"));
    const QCommandLineOption phoneOption(
        QStringLiteral("phone"),
        QStringLiteral("Optionally send PHONE_LOGIN_REQ after heartbeat. May auto-register an unknown number."),
        QStringLiteral("phone"));
    parser.addOption(hostOption);
    parser.addOption(portOption);
    parser.addOption(timeoutOption);
    parser.addOption(phoneOption);

    if (!parser.parse(app.arguments())) {
        qCritical().noquote() << parser.errorText();
        return InvalidArguments;
    }
    if (parser.isSet(helpOption) || parser.isSet(QStringLiteral("help-all"))) {
        QTextStream(stdout) << parser.helpText();
        return Success;
    }
    if (parser.isSet(versionOption)) {
        QTextStream(stdout) << QCoreApplication::applicationName() << ' '
                            << QCoreApplication::applicationVersion() << '\n';
        return Success;
    }

    const QString host = parser.value(hostOption).trimmed();
    bool portOk = false;
    const uint portValue = parser.value(portOption).toUInt(&portOk);
    bool timeoutOk = false;
    const int timeoutMs = parser.value(timeoutOption).toInt(&timeoutOk);
    const QString phone = parser.value(phoneOption).trimmed();

    if (!parser.positionalArguments().isEmpty()) {
        qCritical() << "Unexpected positional arguments.";
        return InvalidArguments;
    }
    if (host.isEmpty()
        || host.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")))) {
        qCritical() << "Host must be non-empty and contain no control characters.";
        return InvalidArguments;
    }
    if (!portOk || portValue == 0 || portValue > 65535) {
        qCritical() << "Port must be an integer from 1 to 65535.";
        return InvalidArguments;
    }
    if (!timeoutOk || timeoutMs <= 0) {
        qCritical() << "Timeout must be a positive integer in milliseconds.";
        return InvalidArguments;
    }
    if (!phone.isEmpty()
        && !QRegularExpression(QStringLiteral("^1[3-9]\\d{9}$"))
                .match(phone)
                .hasMatch()) {
        qCritical() << "Phone must be an 11-digit mainland China mobile number.";
        return InvalidArguments;
    }

    NetworkSmokeRunner runner(host, static_cast<quint16>(portValue), timeoutMs, phone);
    runner.start();
    return app.exec();
}

#include "main.moc"
