#pragma once

#include <QWidget>

class QVariantAnimation;

/** Pure presentation widget for the charging progress ring. */
class ChargingProgressRing final : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal displayedProgress READ displayedProgress WRITE setDisplayedProgress)
public:
    explicit ChargingProgressRing(QWidget *parent = nullptr);
    qreal displayedProgress() const;
    void setDisplayedProgress(qreal value);
    void setIndeterminate(bool indeterminate);
    void setActive(bool active);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    qreal m_progress = 0.0;
    qreal m_phase = 0.0;
    bool m_indeterminate = false;
    bool m_active = false;
    QVariantAnimation *m_phaseAnimation = nullptr;
};
