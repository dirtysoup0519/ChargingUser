#include "loginwindow.h"
#include "ui_loginwindow.h"
#include <QPushButton>

LoginWindow::LoginWindow(QWidget *parent) : QWidget(parent), ui(new Ui::LoginWindow)
{
    ui->setupUi(this);
    setWindowFlag(Qt::FramelessWindowHint);
    connect(ui->loginButton, &QPushButton::clicked, this, [this] {
        emit loginSucceeded();
        hide();
    });
}

LoginWindow::~LoginWindow() { delete ui; }
