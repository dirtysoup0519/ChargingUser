#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QPushButton>

MainWindow::MainWindow(QWidget *parent) : QWidget(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowFlag(Qt::FramelessWindowHint);

    const QList<QPushButton *> stationButtons = {
        ui->stationButton1, ui->stationButton2, ui->stationButton3
    };
    for (QPushButton *button : stationButtons) {
        connect(button, &QPushButton::clicked,
                this, &MainWindow::stationDetailsRequested);
    }
}

MainWindow::~MainWindow() { delete ui; }
