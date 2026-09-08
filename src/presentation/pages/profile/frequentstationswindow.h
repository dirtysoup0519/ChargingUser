#pragma once

#include "presentation/contracts/frequentstationviewstate.h"

#include <QWidget>

class QVBoxLayout;
class QLabel;

class FrequentStationsWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit FrequentStationsWindow(QWidget *parent = nullptr);
    void render(const FrequentStationsViewState &state);

signals:
    void backRequested();
    void stationRequested(const QString &stationId);

private:
    QVBoxLayout *m_cards = nullptr;
    QLabel *m_message = nullptr;
};
