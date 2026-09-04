#pragma once

#include <QWidget>

namespace Ui { class MainWindow; }

class MainWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

signals:
    void stationDetailsRequested();

private:
    Ui::MainWindow *ui;
};
