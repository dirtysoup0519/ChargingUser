#pragma once

#include <QWidget>

namespace Ui { class NavigationWindow; }

class NavigationWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit NavigationWindow(QWidget *parent = nullptr);
    ~NavigationWindow() override;

signals:
    void backRequested();

private:
    Ui::NavigationWindow *ui;
};
