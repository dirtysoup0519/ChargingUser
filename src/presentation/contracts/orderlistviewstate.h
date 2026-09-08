#pragma once
#include <QList>
#include <QMetaType>
#include <QString>

enum class OrderBusinessType { Charging, Reservation, Recharge };
enum class OrderListAction { None, ContinuePayment, ViewCharging, StartReservedCharging, ViewDetails };

struct OrderListItemView
{
    QString businessId;
    QString relatedBusinessId;
    QString stationId;
    QString chargerId;
    OrderBusinessType type = OrderBusinessType::Charging;
    QString stationName;
    QString chargerCode;
    QString createdAtText;
    QString summaryText;
    QString durationText;
    QString energyText;
    QString amountText;
    QString statusText;
    QString statusTone;
    QString actionText;
    OrderListAction action = OrderListAction::ViewDetails;
};

struct OrderListViewState
{
    QList<OrderListItemView> orders;
    QString message;
};

Q_DECLARE_METATYPE(OrderBusinessType)
Q_DECLARE_METATYPE(OrderListAction)
Q_DECLARE_METATYPE(OrderListItemView)
Q_DECLARE_METATYPE(OrderListViewState)
