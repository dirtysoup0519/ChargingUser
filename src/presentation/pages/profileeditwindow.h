#pragma once

#include <QWidget>

#include "profileeditviewstate.h"

namespace Ui { class ProfileEditWindow; }

class ProfileEditWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit ProfileEditWindow(QWidget *parent = nullptr);
    ~ProfileEditWindow() override;

    void render(const ProfileEditViewState &state);

signals:
    void backRequested();
    void profileSaveRequested(const QString &nickname);

private:
    void submitCurrentInput();

    Ui::ProfileEditWindow *ui;
};
