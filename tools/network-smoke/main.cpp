#include "massagehandler.h"
#include "network/clientsocketthreadmanager.h"
#include "protocol.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
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
                       const QString &phone, bool queryStations,
                       QObject *parent = nullptr)
        : QObject(parent)
        , m_channel(host, port, this)
        , m_timeoutMs(timeoutMs)
        , m_phone(phone)
        , m_queryStations(queryStations)
        , m_hostForLog(host)
        , m_portForLog(port)
    {
        m_timeoutTimer.setSingleShot(true);

        connect(&m_channel, &IBackendChannel::connectionStateChanged,
                this, &NetworkSmokeRunner::handleConnectionStateChanged);
        connect(&m_channel, &IBackendChannel::networkError,
                this, &NetworkSmokeRunner::handleNetworkError);
        connect(&m_channel, &IBackendChannel::frameSendFailed,
                this, &NetworkSmokeRunner::handleSendFailed);
        connect(&m_channel, &IBackendChannel::frameReceived,
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
        m_channel.start();
    }

private:
    enum class State
    {
        Connecting,
        WaitingHeartbeat,
        WaitingPhoneLogin,
        WaitingStationQuery
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
        case State::WaitingStationQuery:
            return QStringLiteral("waiting for station DATA (200)");
        }
        return QStringLiteral("waiting for server response");
    }

    void handleConnectionStateChanged(ConnectionState state)
    {
        if (m_finished) {
            return;
        }
        if (state == ConnectionState::Connecting) {
            return;
        }
        if (state != ConnectionState::Connected) {
            fail(TransportFailure,
                 QStringLiteral("Connection entered a non-connected state while %1.")
                     .arg(stateDescription()));
            return;
        }
        qInfo() << "Connected.";
        m_state = State::WaitingHeartbeat;
        m_channel.sendFrame(HEARTBEAT);
        qInfo() << "Sent message type=107 payloadBytes=0.";
    }

    void handleNetworkError(const QString &message)
    {
        if (!m_finished)
            fail(TransportFailure,
                 QStringLiteral("Network error: %1").arg(sanitizeReason(message)));
    }

    void handleSendFailed(int messageType, const QString &message)
    {
        if (!m_finished)
            fail(SendFailure,
                 QStringLiteral("Failed to send type=%1: %2")
                     .arg(messageType)
                     .arg(sanitizeReason(message)));
    }

    void handleFrame(int msgType, const QJsonObject &payload)
    {
        if (m_finished) {
            return;
        }

        qInfo().noquote() << QStringLiteral("Received message type=%1 payloadBytes=%2.")
                                 .arg(msgType)
                                 .arg(QJsonDocument(payload)
                                          .toJson(QJsonDocument::Compact)
                                          .size());

        if (msgType >= 300 && msgType < 400) {
            QString reason = payload.value(QStringLiteral("err")).toString();
            if (reason.isEmpty()) {
                reason = payload.value(QStringLiteral("reason")).toString();
            }
            const QString code = payload.value(QStringLiteral("code")).toString();
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
            m_state = State::WaitingPhoneLogin;
            m_channel.sendFrame(PHONE_LOGIN_REQ, request);
            qInfo().noquote()
                << QStringLiteral("Sent message type=116 for %1; payload not logged.")
                       .arg(maskPhone(m_phone));
            return;
        }

        if (m_state == State::WaitingPhoneLogin && msgType == PHONE_LOGIN_ACK) {
            if (payload.value(QStringLiteral("username")).toString().isEmpty()) {
                fail(ProtocolFailure,
                     QStringLiteral("PHONE_LOGIN_ACK (217) is missing username."));
                return;
            }
            if (!m_queryStations) {
                succeed(QStringLiteral("Heartbeat and phone-login round trips completed."));
                return;
            }

            QJsonObject request;
            request.insert(QStringLiteral("table"), QString::fromLatin1(TBL_STATION));
            request.insert(QStringLiteral("cond"), QJsonObject());
            request.insert(QStringLiteral("requestId"),
                           QUuid::createUuid().toString(QUuid::WithoutBraces));
            m_state = State::WaitingStationQuery;
            m_channel.sendFrame(GETDATA, request);
            qInfo() << "Sent read-only station query type=100; payload not logged.";
            return;
        }

        if (m_state == State::WaitingStationQuery && msgType == DATA) {
            const QJsonValue data = payload.value(QStringLiteral("data"));
            if (!data.isArray()) {
                fail(ProtocolFailure,
                     QStringLiteral("Station DATA (200) is missing data array."));
                return;
            }
            succeed(QStringLiteral("Heartbeat, login, and station query completed; rows=%1.")
                        .arg(data.toArray().size()));
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
        m_channel.stop();
        QCoreApplication::exit(exitCode);
    }

    ClientSocketThreadManager m_channel;
    QTimer m_timeoutTimer;
    int m_timeoutMs;
    QString m_phone;
    bool m_queryStations = false;
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
    const QCommandLineOption queryStationsOption(
        QStringLiteral("query-stations"),
        QStringLiteral("After phone login, issue a read-only GETDATA station query."));
    parser.addOption(hostOption);
    parser.addOption(portOption);
    parser.addOption(timeoutOption);
    parser.addOption(phoneOption);
    parser.addOption(queryStationsOption);

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
    const bool queryStations = parser.isSet(queryStationsOption);

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
    if (queryStations && phone.isEmpty()) {
        qCritical() << "--query-stations requires --phone because the server query requires a user session.";
        return InvalidArguments;
    }

    NetworkSmokeRunner runner(host, static_cast<quint16>(portValue), timeoutMs,
                              phone, queryStations);
    runner.start();
    return app.exec();
}

#include "main.moc"
