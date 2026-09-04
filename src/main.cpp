#include <QApplication>
#include <QFile>
#include "loginwindow.h"
#include "mainwindow.h"
#include "profileeditwindow.h"
#include "stationdetailwindow.h"
#include "navigationwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("智充");
    app.setStyle("Fusion");
    QFile theme(":/styles/theme.qss");
    if (theme.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(theme.readAll()));

    LoginWindow login;
    ProfileEditWindow profileEdit;
    MainWindow window;
    StationDetailWindow stationDetail;
    NavigationWindow navigation;
    // Temporary mock value. Replace it with the backend login response later.
    const bool demoIsNewUser = true;
    QObject::connect(&login, &LoginWindow::loginSucceeded, &window, [&] {
        if (demoIsNewUser)
            profileEdit.show();
        else
            window.show();
    });
    QObject::connect(&profileEdit, &ProfileEditWindow::profileSaved,
                     &window, [&window] { window.show(); });
    QObject::connect(&window, &MainWindow::stationDetailsRequested,
                     &stationDetail, [&] { window.hide(); stationDetail.show(); });
    QObject::connect(&stationDetail, &StationDetailWindow::backRequested,
                     &window, [&] { stationDetail.hide(); window.show(); });
    QObject::connect(&stationDetail, &StationDetailWindow::navigationRequested,
                     &navigation, [&] { stationDetail.hide(); navigation.show(); });
    QObject::connect(&navigation, &NavigationWindow::backRequested,
                     &stationDetail, [&] { navigation.hide(); stationDetail.show(); });
    login.show();
    return app.exec();
}
