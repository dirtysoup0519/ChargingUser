#pragma once
#include "presentation/contracts/paymentviewstates.h"
#include <QWidget>

class QLabel;
class QPushButton;

class SettlementWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit SettlementWindow(QWidget *parent = nullptr);
    void render(const SettlementViewState &state);
signals:
    void backRequested();
    void paymentRequested(const QString &orderId);
private:
    SettlementViewState m_state;
    QLabel *m_amount = nullptr;
    QLabel *m_chargingTime = nullptr;
    QLabel *m_paymentMethod = nullptr;
    QLabel *m_chargerInfo = nullptr;
    QLabel *m_message = nullptr;
    QPushButton *m_pay = nullptr;
};
