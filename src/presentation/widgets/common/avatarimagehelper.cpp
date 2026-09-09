#include "avatarimagehelper.h"

#include <QBuffer>
#include <QFileDialog>
#include <QImage>
#include <QObject>

namespace {
constexpr int kMaximumAvatarBytes = 96 * 1024;

QImage squareImage(const QImage &source)
{
    const int side = qMin(source.width(), source.height());
    return source.copy((source.width() - side) / 2,
                       (source.height() - side) / 2, side, side);
}

bool encodeJpeg(const QImage &source, int side, int quality,
                QByteArray *encoded)
{
    encoded->clear();
    QBuffer buffer(encoded);
    if (!buffer.open(QIODevice::WriteOnly))
        return false;
    return source.scaled(side, side, Qt::IgnoreAspectRatio,
                         Qt::SmoothTransformation)
        .save(&buffer, "JPG", quality);
}
} // namespace

namespace AvatarImageHelper {

QPixmap pixmapFromDataUri(const QString &dataUri)
{
    const int comma = dataUri.indexOf(QLatin1Char(','));
    if (!dataUri.startsWith(QStringLiteral("data:image/")) || comma < 0)
        return {};
    QPixmap pixmap;
    pixmap.loadFromData(QByteArray::fromBase64(dataUri.mid(comma + 1).toLatin1()));
    return pixmap;
}

bool selectFromAlbum(QWidget *parent, QString *dataUri, QPixmap *preview,
                     QString *errorMessage)
{
    const QString path = QFileDialog::getOpenFileName(
        parent, QObject::tr("选择头像"), QString(),
        QObject::tr("图片 (*.png *.jpg *.jpeg *.bmp *.webp)"));
    if (path.isEmpty())
        return false;
    const QImage loaded(path);
    if (loaded.isNull()) {
        *errorMessage = QObject::tr("无法读取这张图片，请重新选择。");
        return false;
    }

    const QImage square = squareImage(loaded);
    QByteArray encoded;
    const int sides[] = {256, 224, 192, 160, 128};
    const int qualities[] = {88, 78, 68, 58, 48};
    for (int side : sides) {
        for (int quality : qualities) {
            if (!encodeJpeg(square, side, quality, &encoded))
                continue;
            const QByteArray uri = QByteArrayLiteral("data:image/jpeg;base64,")
                                   + encoded.toBase64();
            if (uri.size() <= kMaximumAvatarBytes) {
                *dataUri = QString::fromLatin1(uri);
                *preview = QPixmap::fromImage(square.scaled(
                    256, 256, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
                errorMessage->clear();
                return true;
            }
        }
    }
    *errorMessage = QObject::tr("图片压缩后仍超过 96 KB，请选择更简单的图片。");
    return false;
}

} // namespace AvatarImageHelper
