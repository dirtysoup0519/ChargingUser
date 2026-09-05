#pragma once

#include <QWidget>

#include "profileviewstate.h"

namespace Ui { class MainWindow; }

class MainWindow final : public QWidget
{
    Q_OBJECT
public:
    enum class PrimaryPage { Home, Charging, Profile };
    Q_ENUM(PrimaryPage)

    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void renderPrimaryPage(PrimaryPage page);
    void renderProfile(const ProfileViewState &state);
    void renderSecondaryPage(QWidget *page);

signals:
    void primaryPageRequested(MainWindow::PrimaryPage page);
    void stationDetailsRequested(const QString &stationId);
    void profileEditRequested();
    void rechargePageRequested();
    void logoutRequested();

private:
    Ui::MainWindow *ui;
};
