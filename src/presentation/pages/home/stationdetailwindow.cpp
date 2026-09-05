#include "stationdetailwindow.h"
#include "ui_stationdetailwindow.h"
#include <QPushButton>

StationDetailWindow::StationDetailWindow(QWidget *parent)
    : QWidget(parent), ui(new Ui::StationDetailWindow)
{
    ui->setupUi(this);
    setWindowFlag(Qt::FramelessWindowHint);
    connect(ui->backButton, &QPushButton::clicked,
            this, &StationDetailWindow::backRequested);
    connect(ui->navigationButton, &QPushButton::clicked,
            this, &StationDetailWindow::navigationRequested);
    connect(ui->chargeButton, &QPushButton::clicked,
            this, &StationDetailWindow::chargeRequested);
}

StationDetailWindow::~StationDetailWindow() { delete ui; }
