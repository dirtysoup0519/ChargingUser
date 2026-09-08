#pragma once

#include <QWidget>
#include <QMetaType>

class QLabel;
class QLineEdit;
class QPushButton;

enum class PasswordChangeStep { VerifyOriginal, EnterNewPassword };

class PasswordChangeWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit PasswordChangeWindow(QWidget *parent = nullptr);
    void setStep(PasswordChangeStep step, const QString &message = QString());

signals:
    void backRequested();
    void originalPasswordSubmitted(const QString &password);
    void newPasswordSubmitted(const QString &newPassword);

private:
    void submit();
    PasswordChangeStep m_step = PasswordChangeStep::VerifyOriginal;
    QLabel *m_title = nullptr;
    QLabel *m_instruction = nullptr;
    QLabel *m_originalLabel = nullptr;
    QLineEdit *m_originalEdit = nullptr;
    QLabel *m_newLabel = nullptr;
    QLineEdit *m_newEdit = nullptr;
    QLabel *m_confirmLabel = nullptr;
    QLineEdit *m_confirmEdit = nullptr;
    QLabel *m_message = nullptr;
    QPushButton *m_submit = nullptr;
};

Q_DECLARE_METATYPE(PasswordChangeStep)
