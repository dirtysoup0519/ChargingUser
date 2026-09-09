#pragma once

#include <QWidget>
#include <QMetaType>

#include "profileeditviewstate.h"

namespace Ui { class ProfileEditWindow; }
class QLabel;
class QLineEdit;
class QPushButton;

enum class ProfileEditMode { ExistingProfile, PhoneFirstSetup, UsernameFirstSetup };

class ProfileEditWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit ProfileEditWindow(QWidget *parent = nullptr);
    ~ProfileEditWindow() override;

    void render(const ProfileEditViewState &state);
    void setEditMode(ProfileEditMode mode, const QString &username = QString());
    void setAvatarPreview(const QString &avatarDataUri);

signals:
    void backRequested();
    void profileSaveRequested(const QString &nickname);
    void profileCompletionRequested(const QString &nickname,
                                    const QString &phone,
                                    const QString &newPassword);
    void passwordChangeRequested();
    void avatarChangeRequested();

private:
    void submitCurrentInput();

    Ui::ProfileEditWindow *ui;
    ProfileEditMode m_mode = ProfileEditMode::ExistingProfile;
    QLabel *m_newPasswordLabel = nullptr;
    QLineEdit *m_newPasswordEdit = nullptr;
    QLabel *m_confirmPasswordLabel = nullptr;
    QLineEdit *m_confirmPasswordEdit = nullptr;
    QPushButton *m_changePasswordButton = nullptr;
};

Q_DECLARE_METATYPE(ProfileEditMode)
