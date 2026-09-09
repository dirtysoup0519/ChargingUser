#pragma once

class QAbstractScrollArea;
class QWidget;

class DragScrollHelper final
{
public:
    static void enableFor(QWidget *root);
    static void enableFor(QAbstractScrollArea *area);
    static void prioritizeInteractiveWidget(QWidget *widget,
                                             QAbstractScrollArea *area);

private:
    DragScrollHelper() = delete;
};
