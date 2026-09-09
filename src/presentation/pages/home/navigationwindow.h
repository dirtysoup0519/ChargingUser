#pragma once

#include "presentation/contracts/mapviewstates.h"

#include <QWidget>

namespace Ui { class NavigationWindow; }

class NavigationWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit NavigationWindow(QWidget *parent = nullptr);
    ~NavigationWindow() override;

    void render(const NavigationViewState &state);

signals:
    void backRequested();
    void routeModeRequested(TravelMode mode);
    void manualOriginRequested(const QString &address);
    void originCandidateSelected(const QString &candidateId);
    void routeRetryRequested();

private:
    void setOriginEditorOpen(bool open);
    void submitManualOrigin();
    void rebuildOriginCandidates(const QVector<GeocodeCandidateView> &candidates);
    void rebuildRouteSteps(const QVector<RouteStepView> &steps);

    Ui::NavigationWindow *ui;
    NavigationViewState m_state;
    bool m_originEditorOpen = false;
    bool m_originSubmissionPending = false;
};
