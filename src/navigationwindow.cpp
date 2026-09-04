#include "navigationwindow.h"
#include "ui_navigationwindow.h"
#include <QPushButton>

NavigationWindow::NavigationWindow(QWidget *parent)
    : QWidget(parent), ui(new Ui::NavigationWindow)
{
    ui->setupUi(this);
    setWindowFlag(Qt::FramelessWindowHint);
    connect(ui->backButton, &QPushButton::clicked, this, [this] {
        emit backRequested();
        hide();
    });
}

NavigationWindow::~NavigationWindow() { delete ui; }
