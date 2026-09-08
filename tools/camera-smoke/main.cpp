#include <QApplication>
#include <QCamera>
#include <QDebug>
#include <QImage>
#include <QLabel>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QPixmap>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoSink>

namespace {
QImage convertFrame(const QVideoFrame &source)
{
    QImage image = source.toImage();
    if (!image.isNull()) return image;

    QVideoFrame frame(source);
    if (!frame.map(QVideoFrame::ReadOnly)) return {};
    const QImage::Format format =
        QVideoFrameFormat::imageFormatFromPixelFormat(frame.pixelFormat());
    if (format != QImage::Format_Invalid) {
        image = QImage(frame.bits(0), frame.width(), frame.height(),
                       frame.bytesPerLine(0), format).copy();
    }
    frame.unmap();
    return image;
}
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QLabel preview(QStringLiteral("Waiting for camera frames..."));
    preview.setAlignment(Qt::AlignCenter);
    preview.setMinimumSize(640, 480);
    preview.resize(800, 600);
    preview.show();

    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    qInfo() << "Qt cameras:" << cameras.size();
    for (const QCameraDevice &device : cameras)
        qInfo() << "camera" << device.description() << device.id();
    if (cameras.isEmpty()) {
        qCritical() << "No camera reported by Qt Multimedia.";
        preview.setText(QStringLiteral("No camera reported by Qt Multimedia"));
        return app.exec();
    }

    QCamera camera(cameras.front());
    QMediaCaptureSession session;
    QVideoSink sink;
    session.setCamera(&camera);
    session.setVideoSink(&sink);

    int frameCount = 0;
    bool imageRendered = false;
    QObject::connect(&camera, &QCamera::activeChanged,
                     [](bool active) { qInfo() << "camera active:" << active; });
    QObject::connect(&camera, &QCamera::errorOccurred,
                     [&](QCamera::Error error, const QString &description) {
        qCritical() << "camera error:" << int(error) << description;
        preview.setText(QStringLiteral("Camera error: %1").arg(description));
    });
    QObject::connect(&sink, &QVideoSink::videoFrameChanged, &preview,
                     [&](const QVideoFrame &frame) {
        ++frameCount;
        if (frameCount <= 3) {
            qInfo() << "frame" << frameCount << frame.size()
                    << "pixelFormat" << int(frame.pixelFormat())
                    << "valid" << frame.isValid();
        }
        const QImage image = convertFrame(frame);
        if (image.isNull()) {
            if (frameCount <= 3) qWarning() << "frame conversion failed";
            return;
        }
        preview.setPixmap(QPixmap::fromImage(image).scaled(
            preview.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        if (!imageRendered) {
            imageRendered = true;
            const bool saved = image.save(QStringLiteral("/tmp/camera-smoke-first-frame.png"));
            qInfo() << "first frame rendered; saved:" << saved;
        }
    }, Qt::QueuedConnection);

    QTimer::singleShot(5000, [&] {
        qInfo() << "five-second result: frames=" << frameCount
                << "imageRendered=" << imageRendered;
        if (frameCount == 0)
            preview.setText(QStringLiteral("Camera active, but Qt received no frames"));
        else if (!imageRendered)
            preview.setText(QStringLiteral("Frames received, but conversion failed"));
    });

    camera.start();
    return app.exec();
}
