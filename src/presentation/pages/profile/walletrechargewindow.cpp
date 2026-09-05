#include "walletrechargewindow.h"
#include "ui_walletrechargewindow.h"

#include <QButtonGroup>
#include <QLineEdit>
#include <QList>
#include <QPair>
#include <QPushButton>

WalletRechargeWindow::WalletRechargeWindow(QWidget *parent)
    : QWidget(parent), ui(new Ui::WalletRechargeWindow)
{
    ui->setupUi(this);
    setWindowFlag(Qt::FramelessWindowHint);

    auto *amountGroup = new QButtonGroup(this);
    amountGroup->setExclusive(true);
    const QList<QPair<QPushButton *, QString>> amounts = {
        {ui->amount20Button, QStringLiteral("20")},
        {ui->amount50Button, QStringLiteral("50")},
        {ui->amount100Button, QStringLiteral("100")},
        {ui->amount200Button, QStringLiteral("200")}
    };
    for (const auto &entry : amounts) {
        amountGroup->addButton(entry.first);
        connect(entry.first, &QPushButton::clicked, this,
                [this, entry] { selectQuickAmount(entry.second); });
    }

    connect(ui->customAmountEdit, &QLineEdit::textEdited, this,
            [this, amountGroup](const QString &amountText) {
        if (auto *checked = amountGroup->checkedButton()) {
            amountGroup->setExclusive(false);
            checked->setChecked(false);
            amountGroup->setExclusive(true);
        }
        ui->confirmRechargeButton->setText(amountText.isEmpty()
                                           ? tr("确认充值")
                                           : tr("确认充值  ¥%1").arg(amountText));
    });
    connect(ui->backButton, &QPushButton::clicked,
            this, &WalletRechargeWindow::backRequested);
    connect(ui->confirmRechargeButton, &QPushButton::clicked, this, [this] {
        emit rechargeRequested(ui->customAmountEdit->text().trimmed());
    });
}

WalletRechargeWindow::~WalletRechargeWindow() { delete ui; }

void WalletRechargeWindow::renderBalance(const QString &balanceText)
{
    ui->balanceValueLabel->setText(balanceText.isEmpty()
                                   ? QStringLiteral("--") : balanceText);
}

void WalletRechargeWindow::selectQuickAmount(const QString &amountText)
{
    ui->customAmountEdit->setText(amountText);
    ui->confirmRechargeButton->setText(tr("确认充值  ¥%1").arg(amountText));
}
