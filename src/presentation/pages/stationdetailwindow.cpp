#include "stationdetailwindow.h"
#include "ui_stationdetailwindow.h"
#include <QPushButton>

StationDetailWindow::StationDetailWindow(QWidget *parent)
    : QWidget(parent), ui(new Ui::StationDetailWindow)
{
    ui->setupUi(this);
    setWindowFlag(Qt::FramelessWindowHint);
    connect(ui->backButton, &QPushButton::clicked, this, [this] {
        emit backRequested();
        hide();
    });
    connect(ui->navigationButton, &QPushButton::clicked, this, [this] {
        emit navigationRequested();
        hide();
    });
    connect(ui->chargeButton, &QPushButton::clicked,
            this, &StationDetailWindow::chargeRequested);
}

StationDetailWindow::~StationDetailWindow() { delete ui; }
