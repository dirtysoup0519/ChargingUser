#include "chargingprogressring.h"

#include <QPainter>
#include <QPainterPath>
#include <QGradient>
#include <QSizePolicy>
#include <QVariantAnimation>
#include <QtGlobal>

ChargingProgressRing::ChargingProgressRing(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(246, 246);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_phaseAnimation = new QVariantAnimation(this);
    m_phaseAnimation->setStartValue(0.0);
    m_phaseAnimation->setEndValue(1.0);
    m_phaseAnimation->setDuration(2200);
    m_phaseAnimation->setLoopCount(-1);
    connect(m_phaseAnimation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value) { m_phase = value.toReal(); update(); });
}

qreal ChargingProgressRing::displayedProgress() const { return m_progress; }

void ChargingProgressRing::setDisplayedProgress(qreal value)
{
    m_progress = qBound<qreal>(0.0, value, 100.0);
    update();
}

void ChargingProgressRing::setIndeterminate(bool indeterminate)
{
    m_indeterminate = indeterminate;
    update();
}

void ChargingProgressRing::setActive(bool active)
{
    m_active = active;
    if (active) m_phaseAnimation->start();
    else m_phaseAnimation->stop();
    update();
}

void ChargingProgressRing::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal side = qMin(width(), height()) - 26.0;
    const QRectF ring((width() - side) / 2.0, (height() - side) / 2.0, side, side);
    QPen track(QColor("#E5EEFC"), 15, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(track);
    painter.drawArc(ring, 0, 360 * 16);

    QConicalGradient gradient(ring.center(), 90.0 - (m_active ? m_phase * 360.0 : 0.0));
    gradient.setColorAt(0.0, QColor("#08B9F5"));
    gradient.setColorAt(0.48, QColor("#1478FF"));
    gradient.setColorAt(1.0, QColor("#075EEA"));
    QPen progress(QBrush(gradient), 15, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(progress);
    const qreal span = m_indeterminate ? 92.0 : (m_progress * 3.6);
    const qreal start = 90.0 + (m_active ? m_phase * 360.0 : 0.0);
    painter.drawArc(ring, qRound(start * 16.0), qRound(span * 16.0));
}
