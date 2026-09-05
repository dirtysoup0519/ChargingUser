#pragma once

#include <QWidget>

#include "loginviewstate.h"

namespace Ui { class LoginWindow; }

class LoginWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit LoginWindow(QWidget *parent = nullptr);
    ~LoginWindow() override;

    void render(const LoginViewState &state);

signals:
    void loginRequested(const QString &phone);

private:
    void submitCurrentInput();

    Ui::LoginWindow *ui;
};
