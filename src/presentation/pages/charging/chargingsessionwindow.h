#pragma once

#include "presentation/contracts/chargingsessionviewstate.h"

#include <QWidget>

namespace Ui { class ChargingSessionWindow; }
class QPropertyAnimation;
class QFrame;
class QLabel;
class QPushButton;
class QResizeEvent;

/** Displays the selected charging order's live progress. */
class ChargingSessionWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit ChargingSessionWindow(QWidget *parent = nullptr);
    ~ChargingSessionWindow() override;
    void setEmbeddedMode(bool embedded);
    void render(const ChargingSessionViewState &state);
    void renderSessions(const ChargingSessionCollectionViewState &state);

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

    Ui::ChargingSessionWindow *ui;
    ChargingSessionViewState m_state;
    ChargingSessionCollectionViewState m_collection;
    QPropertyAnimation *m_progressAnimation = nullptr;
    QFrame *m_emptyState = nullptr;
    QPushButton *m_backButton = nullptr;
};
