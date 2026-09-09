#pragma once

#include <QWidget>
#include <QMetaType>

class QLabel;
class QLineEdit;
class QPushButton;

class PasswordChangeWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit PasswordChangeWindow(QWidget *parent = nullptr);
    void reset(const QString &message = QString());
    void setSubmitting(bool submitting, const QString &message = QString());

signals:
    void backRequested();
    void passwordSubmitted(const QString &oldPassword, const QString &newPassword);

private:
    void submit();
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
