#pragma once

#include <QWidget>

class QLabel;

class ProfileTextWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit ProfileTextWindow(QWidget *parent = nullptr);
    void renderContent(const QString &title, const QString &body);

signals:
    void backRequested();

private:
    QLabel *m_title = nullptr;
    QLabel *m_body = nullptr;
};
