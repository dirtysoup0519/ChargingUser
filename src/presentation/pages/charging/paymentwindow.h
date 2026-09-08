#pragma once
#include "presentation/contracts/paymentviewstates.h"
#include <QWidget>

class QLabel;
class QPushButton;

class PaymentWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit PaymentWindow(QWidget *parent = nullptr);
    void render(const PaymentViewState &state);
signals:
    void backRequested();
    void rechargeRequested();
    void payRequested(const QString &businessId, PaymentPurpose purpose);
    void paymentResultRefreshRequested(const QString &businessId);
private:
    PaymentViewState m_state;
    QLabel *m_title = nullptr;
    QLabel *m_description = nullptr;
    QLabel *m_amount = nullptr;
    QLabel *m_balance = nullptr;
    QLabel *m_message = nullptr;
    QPushButton *m_recharge = nullptr;
    QPushButton *m_pay = nullptr;
};
