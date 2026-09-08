#include "walletrechargewindow.h"
#include "dragscrollhelper.h"
#include "ui_walletrechargewindow.h"

#include <QButtonGroup>
#include <QLineEdit>
#include <QList>
#include <QPair>
#include <QPushButton>
#include <QLabel>
#include <QStringList>
#include <QVBoxLayout>

WalletRechargeWindow::WalletRechargeWindow(QWidget *parent)
    : QWidget(parent), ui(new Ui::WalletRechargeWindow)
{
    ui->setupUi(this);
    DragScrollHelper::enableFor(this);

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
    auto *transactions = new QLabel(this);
    transactions->setObjectName(QStringLiteral("walletTransactionsLabel"));
    transactions->setWordWrap(true);
    transactions->setStyleSheet(QStringLiteral(
        "#walletTransactionsLabel { background: white; border-radius: 8px; "
        "padding: 10px; color: #52627A; font-size: 13px; }"));
    transactions->setText(tr("暂无钱包流水"));
    ui->contentLayout->insertWidget(5, transactions);
}

WalletRechargeWindow::~WalletRechargeWindow() { delete ui; }

void WalletRechargeWindow::renderBalance(const QString &balanceText)
{
    ui->balanceValueLabel->setText(balanceText.isEmpty()
                                   ? QStringLiteral("--") : balanceText);
}

void WalletRechargeWindow::render(const WalletViewState &state)
{
    renderBalance(state.balanceText);
    const bool busy = state.status == WalletPageStatus::Loading
                      || state.status == WalletPageStatus::Submitting;
    ui->loadingIndicator->setVisible(busy);
    ui->loadingIndicator->setText(state.status == WalletPageStatus::Loading
                                      ? tr("正在刷新钱包数据…")
                                      : tr("正在提交充值请求…"));
    ui->errorLabel->setVisible(!state.message.isEmpty());
    ui->errorLabel->setText(state.message);
    ui->confirmRechargeButton->setEnabled(state.canSubmit && !busy);
    ui->customAmountEdit->setEnabled(!busy);
    ui->amount20Button->setEnabled(!busy);
    ui->amount50Button->setEnabled(!busy);
    ui->amount100Button->setEnabled(!busy);
    ui->amount200Button->setEnabled(!busy);
    auto *transactions = findChild<QLabel *>(QStringLiteral("walletTransactionsLabel"));
    if (transactions) {
        QStringList lines;
        for (const WalletTransaction &transaction : state.recentTransactions) {
            QString type = tr("其他");
            if (transaction.type == WalletTransactionType::Recharge)
                type = tr("充值");
            else if (transaction.type == WalletTransactionType::Payment)
                type = tr("支付");
            else if (transaction.type == WalletTransactionType::Refund)
                type = tr("退款");
            const QString time = transaction.createdAtUtc.isValid()
                ? transaction.createdAtUtc.toLocalTime().toString(QStringLiteral("MM-dd hh:mm"))
                : tr("时间未知");
            const QString sign = transaction.amountCents >= 0 ? QStringLiteral("+") : QString();
            lines.append(tr("%1  %2  %3¥%4")
                .arg(time, type, sign)
                .arg(qAbs(transaction.amountCents) / 100.0, 0, 'f', 2));
        }
        transactions->setText(lines.isEmpty() ? tr("暂无钱包流水") : lines.join(QLatin1Char('\n')));
        transactions->setVisible(state.status != WalletPageStatus::Idle);
    }
}

void WalletRechargeWindow::selectQuickAmount(const QString &amountText)
{
    ui->customAmountEdit->setText(amountText);
    ui->confirmRechargeButton->setText(tr("确认充值  ¥%1").arg(amountText));
}
