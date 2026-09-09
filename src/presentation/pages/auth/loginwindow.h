#pragma once

#include <QWidget>

#include "loginviewstate.h"

namespace Ui { class LoginWindow; }
class LegalDocumentPage;
class QResizeEvent;
class QLineEdit;
class QPushButton;

class LoginWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit LoginWindow(QWidget *parent = nullptr);
    ~LoginWindow() override;

    void render(const LoginViewState &state);

protected:
    void resizeEvent(QResizeEvent *event) override;

signals:
    // Keep credential login as a Qt signal so the real entry can wire it to the user binder.
    void loginRequested(const QString &phone);
    void usernamePasswordLoginRequested(const QString &username,
                                        const QString &password);

private:
    void submitCurrentInput();
    void openLegalDocument(const QString &target);
    void setUsernameLogin(bool enabled);

    Ui::LoginWindow *ui;
    LegalDocumentPage *m_legalPage = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QPushButton *m_loginModeButton = nullptr;
    bool m_usernameLogin = false;
};
