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

    /**
     * Adds a secondary page to the managed page stack without navigating to it.
     * The stack then owns the page and is the only component controlling its
     * visibility, so a newly created child page cannot cover the primary page.
     */
    void registerSecondaryPage(QWidget *page);
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
