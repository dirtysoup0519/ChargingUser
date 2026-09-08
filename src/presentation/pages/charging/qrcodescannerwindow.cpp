#include "qrcodescannerwindow.h"
#include "ui_qrcodescannerwindow.h"

#include <QPushButton>
#include <QStyle>

#ifdef CHARGINGUSER_ENABLE_QT_MULTIMEDIA
#include <QCamera>
#include <QCameraFormat>
#include <QMediaDevices>
#include <QMediaCaptureSession>
#include <QPixmap>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoSink>
#endif

#ifdef CHARGINGUSER_ENABLE_QT_MULTIMEDIA
namespace {
QImage imageFromVideoFrame(const QVideoFrame &source)
{
    QImage image = source.toImage();
    if (!image.isNull()) return image;

    QVideoFrame frame(source);
    if (!frame.map(QVideoFrame::ReadOnly)) return {};
    const QImage::Format imageFormat =
        QVideoFrameFormat::imageFormatFromPixelFormat(frame.pixelFormat());
    if (imageFormat != QImage::Format_Invalid) {
        image = QImage(frame.bits(0), frame.width(), frame.height(),
                       frame.bytesPerLine(0), imageFormat).copy();
    }
    frame.unmap();
    return image;
}
}
#endif

QrCodeScannerWindow::QrCodeScannerWindow(QWidget *parent)
    : QWidget(parent), ui(new Ui::QrCodeScannerWindow)
{
    ui->setupUi(this);
    connect(ui->backButton, &QPushButton::clicked, this, &QrCodeScannerWindow::backRequested);
    connect(ui->permissionButton, &QPushButton::clicked,
            this, &QrCodeScannerWindow::cameraPermissionRequested);
    connect(ui->retryButton, &QPushButton::clicked,
            this, &QrCodeScannerWindow::scanRetryRequested);
    connect(ui->importButton, &QPushButton::clicked,
            this, &QrCodeScannerWindow::imageImportRequested);
    connect(ui->torchButton, &QPushButton::clicked, this, [this] {
        emit torchToggleRequested(!m_state.torchEnabled);
    });
#ifdef CHARGINGUSER_ENABLE_QT_MULTIMEDIA
    const auto cameras = QMediaDevices::videoInputs();
    if (!cameras.isEmpty()) {
        m_camera = new QCamera(cameras.front(), this);
        QCameraFormat selectedFormat;
        for (const QCameraFormat &format : cameras.front().videoFormats()) {
            if (format.resolution() == QSize(640, 480) && format.maxFrameRate() >= 25.0) {
                selectedFormat = format;
                break;
            }
        }
        if (!selectedFormat.isNull())
            m_camera->setCameraFormat(selectedFormat);
        m_captureSession = new QMediaCaptureSession(this);
        m_videoSink = new QVideoSink(this);
        m_captureSession->setCamera(m_camera);
        m_captureSession->setVideoSink(m_videoSink);
        connect(m_videoSink, &QVideoSink::videoFrameChanged, this,
                [this](const QVideoFrame &frame) {
            m_receivedCameraFrame = true;
            if (!m_state.cameraPermissionGranted) return;
            const QImage image = imageFromVideoFrame(frame);
            if (image.isNull()) return;
            m_convertedCameraFrame = true;
            ui->previewPlaceholder->setPixmap(QPixmap::fromImage(image).scaled(
                ui->previewPlaceholder->size(), Qt::KeepAspectRatioByExpanding,
                Qt::SmoothTransformation));
        });
        connect(m_camera, &QCamera::errorOccurred, this,
                [this](QCamera::Error, const QString &description) {
            ui->stateLabel->setText(description.isEmpty()
                                        ? tr("摄像头启动失败")
                                        : tr("摄像头错误：%1").arg(description));
            emit cameraStatusChanged(false, false);
        });
    }
#endif
    render(ScanViewState{});
}

QrCodeScannerWindow::~QrCodeScannerWindow() { delete ui; }

bool QrCodeScannerWindow::cameraAvailable() const
{
#ifdef CHARGINGUSER_ENABLE_QT_MULTIMEDIA
    return m_camera != nullptr;
#else
    return false;
#endif
}

void QrCodeScannerWindow::render(const ScanViewState &state)
{
    m_state = state;
    ui->chargerLabel->setText(state.chargerDisplayText.isEmpty()
                                  ? tr("请将充电桩二维码放入框内")
                                  : tr("正在扫描 %1").arg(state.chargerDisplayText));
    QString message = state.message;
    if (message.isEmpty()) {
        if (state.status == ScanStatus::RequestingPermission)
            message = tr("正在申请摄像头权限…");
        else if (state.status == ScanStatus::OpeningCamera)
            message = tr("正在打开摄像头…");
        else if (state.status == ScanStatus::Scanning)
            message = tr("对准二维码后将自动识别");
        else if (state.status == ScanStatus::Validating)
            message = tr("正在核验充电桩…");
    }
    ui->stateLabel->setText(message);
    ui->stateLabel->setProperty("state", state.status == ScanStatus::Error
                                             ? "error" : "neutral");
    ui->stateLabel->style()->unpolish(ui->stateLabel);
    ui->stateLabel->style()->polish(ui->stateLabel);
    ui->previewPlaceholder->setText(state.cameraAvailable
                                         ? tr("摄像头画面接入区域")
                                         : tr("未检测到摄像头\n可从相册选择二维码"));
#ifdef CHARGINGUSER_ENABLE_QT_MULTIMEDIA
    if (m_videoSink) {
        const bool showPreview = state.cameraPermissionGranted
            && (state.status == ScanStatus::OpeningCamera || state.status == ScanStatus::Scanning);
        ui->previewPlaceholder->setVisible(true);
        if (showPreview)
            ui->previewPlaceholder->setText(QString());
        else
            ui->previewPlaceholder->setPixmap(QPixmap());
        if (showPreview && !m_camera->isActive())
        {
            m_receivedCameraFrame = false;
            m_convertedCameraFrame = false;
            m_camera->start();
            QTimer::singleShot(3000, this, [this] {
                if (!m_camera || !m_camera->isActive() || m_convertedCameraFrame) return;
                ui->stateLabel->setText(m_receivedCameraFrame
                    ? tr("摄像头有视频帧，但当前像素格式无法转换")
                    : tr("摄像头已打开，但 3 秒内没有收到画面帧"));
            });
        }
        if (!showPreview && m_camera->isActive())
            m_camera->stop();
    }
#endif
    ui->permissionButton->setVisible(!state.cameraPermissionGranted
                                     && state.cameraAvailable);
    ui->retryButton->setVisible(state.canRetry);
    ui->importButton->setVisible(state.canImportImage);
    ui->torchButton->setVisible(state.torchSupported);
    ui->torchButton->setText(state.torchEnabled ? tr("关闭手电筒") : tr("打开手电筒"));
}
