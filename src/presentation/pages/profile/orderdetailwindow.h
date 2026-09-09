#pragma once

#include "presentation/contracts/orderdetailviewstate.h"

#include <QWidget>

class QLabel;
class QPushButton;

/** Displays a server-provided order snapshot and emits semantic user intent. */
class OrderDetailWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit OrderDetailWindow(QWidget *parent = nullptr);
    void render(const OrderDetailViewState &state);

signals:
    void backRequested();
    void actionRequested(const QString &businessId,
                         OrderBusinessType type,
                         OrderListAction action);

private:
    OrderDetailViewState m_state;
    QLabel *m_kind = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_amount = nullptr;
    QLabel *m_station = nullptr;
    QLabel *m_charger = nullptr;
    QLabel *m_createdAt = nullptr;
    QLabel *m_duration = nullptr;
    QLabel *m_energy = nullptr;
    QLabel *m_paymentMethod = nullptr;
    QLabel *m_orderId = nullptr;
    QLabel *m_message = nullptr;
    QPushButton *m_action = nullptr;
};
