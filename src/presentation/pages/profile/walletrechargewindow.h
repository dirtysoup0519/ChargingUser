#pragma once

#include "presentation/contracts/walletviewstate.h"

#include <QWidget>

namespace Ui { class WalletRechargeWindow; }

class WalletRechargeWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit WalletRechargeWindow(QWidget *parent = nullptr);
    ~WalletRechargeWindow() override;

    void renderBalance(const QString &balanceText);
    void render(const WalletViewState &state);

signals:
    void backRequested();
    void rechargeRequested(const QString &amountText);

private:
    void selectQuickAmount(const QString &amountText);

    Ui::WalletRechargeWindow *ui;
};
