#include "sessionselectorcombobox.h"
#include <QPainter>
#include <QStyle>
#include <QStyleOptionComboBox>

SessionSelectorComboBox::SessionSelectorComboBox(QWidget *parent) : QComboBox(parent) {}

void SessionSelectorComboBox::paintEvent(QPaintEvent *)
{
    QStyleOptionComboBox option;
    initStyleOption(&option);
    option.currentText.clear();
    option.currentIcon = QIcon();
    QPainter painter(this);
    style()->drawComplexControl(QStyle::CC_ComboBox, &option, &painter, this);
    style()->drawControl(QStyle::CE_ComboBoxLabel, &option, &painter, this);
}

void SessionSelectorComboBox::showPopup()
{
    emit selectorRequested();
}
