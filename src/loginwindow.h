#pragma once

#include <QWidget>

namespace Ui { class LoginWindow; }

class LoginWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit LoginWindow(QWidget *parent = nullptr);
    ~LoginWindow() override;

signals:
    void loginSucceeded();

private:
    Ui::LoginWindow *ui;
};
