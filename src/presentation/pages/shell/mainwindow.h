#pragma once

#include <QWidget>

#include "presentation/contracts/mapviewstates.h"
#include "profileviewstate.h"

#include <QHash>
#include <optional>

namespace Ui { class MainWindow; }
class QPushButton;

class MainWindow final : public QWidget
{
    Q_OBJECT
public:
    enum class PrimaryPage { Home, Charging, Profile };
    Q_ENUM(PrimaryPage)

    explicit MainWindow(QWidget *parent = nullptr);
    void setMapKey(const QString &key);
    ~MainWindow() override;

    /**
     * Adds a secondary page to the managed page stack without navigating to it.
     * The stack then owns the page and is the only component controlling its
     * visibility, so a newly created child page cannot cover the primary page.
     */
    void registerSecondaryPage(QWidget *page);
    void renderPrimaryPage(PrimaryPage page);
    void renderProfile(const ProfileViewState &state);
    void renderHome(const HomeMapViewState &state);
    void renderSecondaryPage(QWidget *page);

signals:
    void primaryPageRequested(MainWindow::PrimaryPage page);
    void stationDetailsRequested(const QString &stationId);
    void mapReady();
    void mapLoadFailed();
    void locateRequested();
    void stationSearchRequested(const QString &keyword);
    void stationSearchRetryRequested();
    void stationSearchCleared();
    void searchAreaRequested(const GeoBounds &bounds);
    void stationSelected(const QString &stationId);
    void profileEditRequested();
    void rechargePageRequested();
    void ordersPageRequested();
    void frequentStationsRequested();
    void feedbackRequested();
    void aboutRequested();
    void logoutRequested();
    void activeReservationRequested(const QString &reservationId,
                                    const QString &stationId,
                                    const QString &chargerId);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class StationSortMode { Distance, Availability };

    void rebuildStationRows(const QVector<StationListItemView> &stations);
    QPushButton *createStationButton(const StationListItemView &station);
    void updateStationButton(QPushButton *button,
                             const StationListItemView &station);
    void applyStationOrder(const QString &selectedStationId);
    void selectStation(const QString &stationId, bool emitIntent);
    void updateHomeReservationCountdown();

    Ui::MainWindow *ui;
    QHash<QString, QPushButton *> m_stationButtons;
    QVector<StationListItemView> m_stationItems;
    QString m_selectedStationId;
    StationSortMode m_stationSortMode = StationSortMode::Distance;
    quint64 m_cameraRevision = 0;
    class QFrame *m_homeReservationCard = nullptr;
    class QLabel *m_homeReservationTitle = nullptr;
    class QLabel *m_homeReservationStation = nullptr;
    class QLabel *m_homeReservationCountdown = nullptr;
    class QTimer *m_homeReservationTimer = nullptr;
    std::optional<ActiveReservationView> m_homeActiveReservation;
};
