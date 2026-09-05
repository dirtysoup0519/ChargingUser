#include <QApplication>
#include <QFile>
#include "loginwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("智充");
    app.setStyle("Fusion");
    QFile theme(":/styles/theme.qss");
    if (theme.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(theme.readAll()));

    LoginWindow login;
    login.show();
    return app.exec();
}
