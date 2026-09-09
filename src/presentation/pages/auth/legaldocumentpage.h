#pragma once

#include <QWidget>

class QLabel;

/** In-app reader for short legal notices shown before authentication. */
class LegalDocumentPage final : public QWidget
{
    Q_OBJECT
public:
    explicit LegalDocumentPage(QWidget *parent = nullptr);

    /** Replaces the visible title and body without navigating away from login. */
    void showDocument(const QString &title, const QString &body);

signals:
    void backRequested();

private:
    QLabel *m_title = nullptr;
    QLabel *m_body = nullptr;
};
