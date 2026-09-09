#pragma once

#include "presentation/contracts/chargingsessionviewstate.h"

#include <QWidget>

namespace Ui { class ChargingSessionWindow; }
class QPropertyAnimation;
class QListWidget;
class QFrame;
class QLabel;
class QResizeEvent;

/** Displays one selected charging order and lets the user switch active orders. */
class ChargingSessionWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit ChargingSessionWindow(QWidget *parent = nullptr);
    ~ChargingSessionWindow() override;
    void render(const ChargingSessionViewState &state);
    void renderSessions(const ChargingSessionCollectionViewState &state);

protected:
    void resizeEvent(QResizeEvent *event) override;

signals:
    void backRequested();
    void scanChargingRequested();
    void activeSessionsRequested();
    void activeSessionSelected(const QString &orderId);
    void refreshRequested();
    void stopChargingRequested();
    void recoverStopResultRequested();

private:
    void animateProgress(int progress);
    void applySelectedSession(const QString &orderId);
    void positionSessionChooser();

    Ui::ChargingSessionWindow *ui;
    ChargingSessionViewState m_state;
    ChargingSessionCollectionViewState m_collection;
    QPropertyAnimation *m_progressAnimation = nullptr;
    QFrame *m_sessionChooserPanel = nullptr;
    QFrame *m_emptyState = nullptr;
    QListWidget *m_sessionChooser = nullptr;
    bool m_updatingSelector = false;
};
