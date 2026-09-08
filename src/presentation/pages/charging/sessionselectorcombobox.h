#pragma once
#include <QComboBox>
class SessionSelectorComboBox final : public QComboBox
{
    Q_OBJECT
public:
    explicit SessionSelectorComboBox(QWidget *parent = nullptr);
signals:
    void selectorRequested();
protected:
    void paintEvent(QPaintEvent *event) override;
    void showPopup() override;
};
