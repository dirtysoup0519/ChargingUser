#pragma once

#include <QWidget>

namespace Ui { class ProfileEditWindow; }

class ProfileEditWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit ProfileEditWindow(QWidget *parent = nullptr);
    ~ProfileEditWindow() override;

signals:
    void profileSaved();

private:
    Ui::ProfileEditWindow *ui;
};
