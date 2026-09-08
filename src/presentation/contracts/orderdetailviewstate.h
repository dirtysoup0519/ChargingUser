#pragma once

#include "orderlistviewstate.h"

#include <QMetaType>
#include <QString>

/** Pure presentation state for one charging or reservation order.
 *  All monetary and time fields are formatted by the Binder; the page never
 *  calculates settlement values or infers an action from visible text.
 */
struct OrderDetailViewState
{
    QString businessId;
    QString relatedBusinessId;
    QString stationId;
    QString chargerId;
    OrderBusinessType type = OrderBusinessType::Charging;
    QString titleText;
    QString stationName;
    QString chargerCode;
    QString createdAtText;
    QString durationText;
    QString energyText;
    QString amountText;
    QString paymentMethodText;
    QString statusText;
    QString statusTone;
    QString message;
    QString actionText;
    OrderListAction action = OrderListAction::None;
    bool actionEnabled = false;
};

Q_DECLARE_METATYPE(OrderDetailViewState)
