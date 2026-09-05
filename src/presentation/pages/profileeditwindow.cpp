#include "profileeditwindow.h"
#include "ui_profileeditwindow.h"
#include <QPushButton>

ProfileEditWindow::ProfileEditWindow(QWidget *parent)
    : QWidget(parent), ui(new Ui::ProfileEditWindow)
{
    ui->setupUi(this);
    setWindowFlag(Qt::FramelessWindowHint);
    connect(ui->saveButton, &QPushButton::clicked, this, [this] {
        emit profileSaved();
        hide();
    });
}

ProfileEditWindow::~ProfileEditWindow() { delete ui; }
