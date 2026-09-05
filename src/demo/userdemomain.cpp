#include <QApplication>
#include <QFile>

#include "app/application.h"
#include "demo/userdemocontroller.h"
#include "modules/user/mockusernetworkapi.h"
#include "presentation/pages/loginwindow.h"
#include "presentation/pages/mainwindow.h"
#include "presentation/pages/profileeditwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("智充用户流程 Demo"));
    app.setStyle(QStringLiteral("Fusion"));

    QFile theme(QStringLiteral(":/styles/theme.qss"));
    if (theme.open(QIODevice::ReadOnly)) {
        app.setStyleSheet(QString::fromUtf8(theme.readAll()));
    }

    MockUserNetworkApi network;
    UserApplicationAssembly assembly(&network);

    LoginWindow login;
    ProfileEditWindow profileEdit;
    MainWindow mainWindow;
    UserDemoController controller(&network, assembly.userUiBinder(),
                                  &login, &profileEdit, &mainWindow);
    controller.showInitialPage();

    return app.exec();
}
