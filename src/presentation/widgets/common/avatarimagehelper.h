#pragma once

#include <QPixmap>
#include <QString>

class QWidget;

namespace AvatarImageHelper {

bool selectFromAlbum(QWidget *parent, QString *dataUri, QPixmap *preview,
                     QString *errorMessage);
QPixmap pixmapFromDataUri(const QString &dataUri);

} // namespace AvatarImageHelper
