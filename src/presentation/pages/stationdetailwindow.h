#pragma once

#include <QWidget>

namespace Ui { class StationDetailWindow; }

class StationDetailWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit StationDetailWindow(QWidget *parent = nullptr);
    ~StationDetailWindow() override;

signals:
    void backRequested();
    void navigationRequested();
    void chargeRequested();

private:
    Ui::StationDetailWindow *ui;
};
