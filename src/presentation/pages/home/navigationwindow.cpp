#include "navigationwindow.h"
#include "ui_navigationwindow.h"
#include <QPushButton>

NavigationWindow::NavigationWindow(QWidget *parent)
    : QWidget(parent), ui(new Ui::NavigationWindow)
{
    ui->setupUi(this);
    setWindowFlag(Qt::FramelessWindowHint);
    connect(ui->backButton, &QPushButton::clicked,
            this, &NavigationWindow::backRequested);
}

NavigationWindow::~NavigationWindow() { delete ui; }
