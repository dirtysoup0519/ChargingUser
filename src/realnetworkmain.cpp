#include "app/application.h"
#include "app/iuseruibinder.h"
#include "network/backendclient.h"
#include "network/qtnetworktransport.h"
#include "network/realusernetworkapi.h"
#include "presentation/pages/auth/loginwindow.h"
#include "protocol.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
#include <QFile>
#include <QRegularExpression>

namespace
{

QString connectionStateName(ConnectionState state)
{
    switch (state) {
    case ConnectionState::Disconnected:
        return QStringLiteral("disconnected");
    case ConnectionState::Connecting:
        return QStringLiteral("connecting");
    case ConnectionState::Connected:
        return QStringLiteral("connected");
    case ConnectionState::Reconnecting:
        return QStringLiteral("reconnecting");
    }
    return QStringLiteral("unknown");
}

QString navigationTargetName(NavigationTarget target)
{
    switch (target) {
    case NavigationTarget::Login:
        return QStringLiteral("login");
    case NavigationTarget::ProfileEdit:
        return QStringLiteral("profile-edit");
    case NavigationTarget::Home:
        return QStringLiteral("home");
    case NavigationTarget::RestrictedHome:
        return QStringLiteral("restricted-home");
    }
    return QStringLiteral("unknown");
}

bool validHost(const QString &host)
{
    return !host.isEmpty()
           && !host.contains(
               QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")));
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("智充"));
    app.setApplicationVersion(QStringLiteral("1.0"));
    app.setStyle(QStringLiteral("Fusion"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("ChargingUser optional real-network entry."));
    parser.addHelpOption();
    parser.addVersionOption();
    // 目标地址优先级：命令行参数 > 环境变量（CHARGER_SERVER_HOST/PORT）> 协议内置默认。
    // 环境变量面向"远程服务端"联调场景：不把 IP 写进仓库，同一构建可切换本地/远程。
    const QString envHost = qEnvironmentVariable("CHARGER_SERVER_HOST").trimmed();
    const QString envPort = qEnvironmentVariable("CHARGER_SERVER_PORT").trimmed();
    const QString defaultHost = envHost.isEmpty()
                                    ? QString::fromLatin1(SERVER_IP)
                                    : envHost;
    const QString defaultPort = envPort.isEmpty()
                                    ? QString::fromLatin1(SERVER_PORT)
                                    : envPort;
    const QCommandLineOption hostOption(
        QStringLiteral("server-host"), QStringLiteral("Backend host or IP address."),
        QStringLiteral("host"), defaultHost);
    const QCommandLineOption portOption(
        QStringLiteral("server-port"), QStringLiteral("Backend TCP port."),
        QStringLiteral("port"), defaultPort);
    parser.addOption(hostOption);
    parser.addOption(portOption);
    parser.process(app);

    if (!parser.positionalArguments().isEmpty()) {
        qCritical() << "Unexpected positional arguments.";
        return 2;
    }

    const QString host = parser.value(hostOption).trimmed();
    bool portOk = false;
    const uint portValue = parser.value(portOption).toUInt(&portOk);
    if (!validHost(host)) {
        qCritical() << "Server host must be non-empty and contain no control characters.";
        return 2;
    }
    if (!portOk || portValue == 0 || portValue > 65535) {
        qCritical() << "Server port must be an integer from 1 to 65535.";
        return 2;
    }

    QFile theme(QStringLiteral(":/styles/theme.qss"));
    if (theme.open(QIODevice::ReadOnly)) {
        app.setStyleSheet(QString::fromUtf8(theme.readAll()));
    }

    // 生命周期顺序必须保持 transport > backend > network API > application assembly。
    // 以下对象按栈逆序析构，确保所有非拥有指针在使用期间有效。
    QtNetworkTransport transport(host, static_cast<quint16>(portValue));
    BackendClient backend(&transport);
    RealUserNetworkApi network(&backend);
    UserApplicationAssembly assembly(&network);

    LoginWindow login;
    IUserUiBinder *binder = assembly.userUiBinder();

    QObject::connect(&login, &LoginWindow::loginRequested,
                     binder, &IUserUiBinder::loginRequested);
    QObject::connect(binder, &IUserUiBinder::loginViewStateChanged,
                     &login, &LoginWindow::render);
    QObject::connect(binder, &IUserUiBinder::navigationRequested,
                     &login, [&login](NavigationTarget target) {
        if (target == NavigationTarget::Login) {
            login.show();
            login.raise();
            return;
        }
        // 第四阶段不伪造资料协议，也不扩大 UI 页面接线范围。
        qWarning().noquote()
            << QStringLiteral("Navigation target '%1' requested; real-network page handoff is not enabled yet.")
                   .arg(navigationTargetName(target));
    });
    QObject::connect(&backend, &BackendClient::connectionStateChanged,
                     &login, [](ConnectionState state) {
        qInfo().noquote()
            << QStringLiteral("Network state: %1").arg(connectionStateName(state));
    });
    QObject::connect(&backend, &BackendClient::networkError,
                     &login, [](const QString &message) {
        qWarning().noquote() << QStringLiteral("Network error: %1").arg(message);
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     &backend, &BackendClient::shutdown);

    login.render(binder->currentLoginViewState());
    login.show();

    qInfo().noquote()
        << QStringLiteral("Starting real-network entry for %1:%2.")
               .arg(host)
               .arg(portValue);
    backend.start();
    return app.exec();
}
