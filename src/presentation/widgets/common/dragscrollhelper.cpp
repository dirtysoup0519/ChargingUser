#include "dragscrollhelper.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QEasingCurve>
#include <QEvent>
#include <QObject>
#include <QPointer>
#include <QScroller>
#include <QScrollerProperties>
#include <QVariant>
#include <QWidget>

namespace {
class InteractiveScrollGuard final : public QObject
{
public:
    InteractiveScrollGuard(QWidget *widget, QAbstractScrollArea *area)
        : QObject(widget), m_area(area)
    {
        widget->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        Q_UNUSED(watched)
        if (!m_area || !m_area->viewport())
            return false;

        if (event->type() == QEvent::Enter) {
            QScroller::ungrabGesture(m_area->viewport());
        } else if (event->type() == QEvent::Leave
                   || event->type() == QEvent::Hide) {
            DragScrollHelper::enableFor(m_area);
        }
        return false;
    }

private:
    QPointer<QAbstractScrollArea> m_area;
};
}

void DragScrollHelper::enableFor(QWidget *root)
{
    if (!root)
        return;

    if (auto *area = qobject_cast<QAbstractScrollArea *>(root))
        enableFor(area);

    const auto areas = root->findChildren<QAbstractScrollArea *>();
    for (QAbstractScrollArea *area : areas)
        enableFor(area);
}

void DragScrollHelper::enableFor(QAbstractScrollArea *area)
{
    if (!area || !area->viewport())
        return;

    if (auto *itemView = qobject_cast<QAbstractItemView *>(area)) {
        itemView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        itemView->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    }

    QScroller::grabGesture(area->viewport(),
                           QScroller::LeftMouseButtonGesture);
    QScroller *scroller = QScroller::scroller(area->viewport());
    QScrollerProperties properties = scroller->scrollerProperties();
    properties.setScrollMetric(QScrollerProperties::ScrollingCurve,
                               QEasingCurve(QEasingCurve::OutCubic));
    properties.setScrollMetric(QScrollerProperties::FrameRate,
                               QScrollerProperties::Fps60);
    properties.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy,
                               QScrollerProperties::OvershootAlwaysOff);
    properties.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy,
                               QScrollerProperties::OvershootAlwaysOff);
    scroller->setScrollerProperties(properties);
}

void DragScrollHelper::prioritizeInteractiveWidget(
    QWidget *widget, QAbstractScrollArea *area)
{
    if (!widget || !area || !area->viewport())
        return;
    new InteractiveScrollGuard(widget, area);
}
