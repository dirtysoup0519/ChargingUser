#pragma once
#include "presentation/contracts/orderlistviewstate.h"
#include <QWidget>

class QVBoxLayout;
class QPushButton;
class QLabel;

class OrderListWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit OrderListWindow(QWidget *parent = nullptr);
    void render(const OrderListViewState &state);
signals:
    void backRequested();
    void refreshRequested();
    void orderActionRequested(const QString &businessId,
                              OrderBusinessType type,
                              OrderListAction action);
private:
    enum class Filter { All, Charging, Reservation };
    void setFilter(Filter filter);
    void rebuild();
    OrderListViewState m_state;
    Filter m_filter = Filter::All;
    QVBoxLayout *m_cardsLayout = nullptr;
    QPushButton *m_allButton = nullptr;
    QPushButton *m_chargingButton = nullptr;
    QPushButton *m_reservationButton = nullptr;
    QLabel *m_emptyLabel = nullptr;
};
