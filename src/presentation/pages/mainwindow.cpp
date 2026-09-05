#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QList>
#include <QPair>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolButton>

MainWindow::MainWindow(QWidget *parent) : QWidget(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowFlag(Qt::FramelessWindowHint);

    const QList<QPair<QPushButton *, QString>> stationButtons = {
        {ui->stationButton1, QStringLiteral("station-sz-civic-center")},
        {ui->stationButton2, QStringLiteral("station-futian-cbd")},
        {ui->stationButton3, QStringLiteral("station-nanshan-tech-park")}
    };
    for (const auto &entry : stationButtons) {
        connect(entry.first, &QPushButton::clicked, this, [this, entry] {
            emit stationDetailsRequested(entry.second);
        });
    }

    connect(ui->homeNav, &QToolButton::clicked, this, [this] {
        emit primaryPageRequested(PrimaryPage::Home);
    });
    connect(ui->chargeNav, &QToolButton::clicked, this, [this] {
        emit primaryPageRequested(PrimaryPage::Charging);
    });
    connect(ui->profileNav, &QToolButton::clicked, this, [this] {
        emit primaryPageRequested(PrimaryPage::Profile);
    });
    connect(ui->btnEditProfile, &QToolButton::clicked,
            this, &MainWindow::profileEditRequested);
    connect(ui->btnRecharge, &QPushButton::clicked,
            this, &MainWindow::rechargePageRequested);
    connect(ui->btnLogout, &QPushButton::clicked,
            this, &MainWindow::logoutRequested);
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::renderPrimaryPage(PrimaryPage page)
{
    QWidget *target = ui->homePage;
    if (page == PrimaryPage::Charging)
        target = ui->chargingPage;
    else if (page == PrimaryPage::Profile)
        target = ui->profilePage;

    ui->pageStack->setCurrentWidget(target);
    ui->bottomBar->show();
    ui->homeNav->setChecked(page == PrimaryPage::Home);
    ui->chargeNav->setChecked(page == PrimaryPage::Charging);
    ui->profileNav->setChecked(page == PrimaryPage::Profile);
}

void MainWindow::renderProfile(const ProfileViewState &state)
{
    const QString nickname = state.nickname.isEmpty() ? tr("未设置昵称") : state.nickname;
    const QString phone = state.maskedPhone.isEmpty() ? tr("手机号待加载") : state.maskedPhone;
    ui->profileSummaryLabel->setText(nickname);
    ui->profilePhoneLabel->setText(phone);
    ui->walletSummaryLabel->setText(
        state.balanceText.isEmpty() ? QStringLiteral("--") : state.balanceText);

    const bool restricted = state.accountState != AccountDisplayState::Normal;
    QString accountMessage = state.accountMessage;
    if (accountMessage.isEmpty() && state.accountState == AccountDisplayState::Frozen)
        accountMessage = tr("当前账号已受限，部分操作暂不可用。");
    else if (accountMessage.isEmpty() && state.accountState == AccountDisplayState::Unknown)
        accountMessage = tr("账号状态尚未确认，请等待刷新后再操作。");
    ui->accountStatusLabel->setText(accountMessage);
    ui->accountStatusLabel->setVisible(restricted);
    // Frozen 用户按 M1 合同仍可充值和支付；只有状态未确认时暂时禁用充值。
    ui->btnRecharge->setEnabled(state.accountState != AccountDisplayState::Unknown);
}

void MainWindow::renderSecondaryPage(QWidget *page)
{
    if (!page)
        return;
    if (ui->pageStack->indexOf(page) < 0)
        ui->pageStack->addWidget(page);
    ui->pageStack->setCurrentWidget(page);
    ui->bottomBar->hide();
}
