#include "qrcodescannerwindow.h"
#include "ui_qrcodescannerwindow.h"

#include <QPushButton>
#include <QStyle>
#include <QHideEvent>

#ifdef CHARGINGUSER_ENABLE_QT_MULTIMEDIA
#include <QCamera>
#include <QDateTime>
#include <QMediaDevices>
#include <QMediaCaptureSession>
#include <QPixmap>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoSink>
#endif

#ifdef CHARGINGUSER_ENABLE_ZXING
#include <ZXing/BarcodeFormat.h>
#include <ZXing/ReadBarcode.h>
#ifdef CHARGINGUSER_ZXING_LEGACY
#include <ZXing/DecodeHints.h>
#else
#include <ZXing/ImageView.h>
#include <ZXing/ReaderOptions.h>
#endif
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

QString decodeQrFrame(const QImage &source)
{
#ifdef CHARGINGUSER_ENABLE_ZXING
    const QImage image = source.convertToFormat(QImage::Format_Grayscale8);
    const ZXing::ImageView view(image.constBits(), image.width(), image.height(),
                                ZXing::ImageFormat::Lum, image.bytesPerLine());
#ifdef CHARGINGUSER_ZXING_LEGACY
    ZXing::DecodeHints options;
#else
    ZXing::ReaderOptions options;
#endif
    options.setFormats(ZXing::BarcodeFormat::QRCode);
    options.setTryHarder(true);
#ifdef CHARGINGUSER_ZXING_LEGACY
    const ZXing::Result barcode = ZXing::ReadBarcode(view, options);
    return barcode.isValid() ? QString::fromStdWString(barcode.text()) : QString();
#else
    const ZXing::Barcode barcode = ZXing::ReadBarcode(view, options);
    return barcode.isValid() ? QString::fromStdString(barcode.text()) : QString();
#endif
#else
    Q_UNUSED(source)
    return {};
#endif
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
    render(ScanViewState{});
}

QrCodeScannerWindow::~QrCodeScannerWindow()
{
#ifdef CHARGINGUSER_ENABLE_QT_MULTIMEDIA
    destroyCameraPipeline();
#endif
    delete ui;
}

bool QrCodeScannerWindow::cameraAvailable() const
{
#ifdef CHARGINGUSER_ENABLE_QT_MULTIMEDIA
    return !QMediaDevices::videoInputs().isEmpty();
#else
    return false;
#endif
}

void QrCodeScannerWindow::hideEvent(QHideEvent *event)
{
#ifdef CHARGINGUSER_ENABLE_QT_MULTIMEDIA
    destroyCameraPipeline();
#endif
    QWidget::hideEvent(event);
}

#ifdef CHARGINGUSER_ENABLE_QT_MULTIMEDIA
void QrCodeScannerWindow::createCameraPipeline()
{
    if (m_camera) return;
    const auto cameras = QMediaDevices::videoInputs();
    if (cameras.isEmpty()) return;

    m_camera = new QCamera(cameras.front(), this);
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
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_qrDetectionLocked || now - m_lastDecodeAtMs < 300) return;
        m_lastDecodeAtMs = now;
        const QString rawText = decodeQrFrame(image);
        if (rawText.isEmpty()) return;
        m_qrDetectionLocked = true;
        if (m_camera) m_camera->stop();
        emit qrCodeDetected(rawText);
    }, Qt::QueuedConnection);
    connect(m_camera, &QCamera::errorOccurred, this,
            [this](QCamera::Error, const QString &description) {
        ui->stateLabel->setText(description.isEmpty()
                                    ? tr("摄像头启动失败")
                                    : tr("摄像头错误：%1").arg(description));
        emit cameraStatusChanged(false, false);
    });
}

void QrCodeScannerWindow::destroyCameraPipeline()
{
    ++m_cameraGeneration;
    if (m_camera) m_camera->stop();
    if (m_captureSession) {
        m_captureSession->setVideoSink(nullptr);
        m_captureSession->setCamera(nullptr);
    }
    delete m_videoSink;
    delete m_captureSession;
    delete m_camera;
    m_videoSink = nullptr;
    m_captureSession = nullptr;
    m_camera = nullptr;
    m_receivedCameraFrame = false;
    m_convertedCameraFrame = false;
    m_qrDetectionLocked = false;
    m_lastDecodeAtMs = 0;
}

void QrCodeScannerWindow::startCamera(bool allowRestart)
{
    createCameraPipeline();
    if (!m_camera) return;
    m_receivedCameraFrame = false;
    m_convertedCameraFrame = false;
    m_qrDetectionLocked = false;
    const int generation = m_cameraGeneration;
    m_camera->start();
    QTimer::singleShot(3500, this, [this, generation, allowRestart] {
        if (!m_camera || generation != m_cameraGeneration || m_convertedCameraFrame) return;
        if (m_receivedCameraFrame) {
            ui->stateLabel->setText(tr("摄像头有视频帧，但当前像素格式无法转换"));
            return;
        }
        if (!allowRestart) {
            ui->stateLabel->setText(tr("摄像头重建后仍未收到画面，请检查虚拟机 USB 摄像头连接"));
            return;
        }
        ui->stateLabel->setText(tr("正在释放并重建摄像头…"));
        destroyCameraPipeline();
        QTimer::singleShot(2500, this, [this] {
            if (!m_state.cameraPermissionGranted || !isVisible()) return;
            startCamera(false);
        });
    });
}
#endif

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
    const bool showPreview = state.cameraPermissionGranted
        && (state.status == ScanStatus::OpeningCamera || state.status == ScanStatus::Scanning);
    ui->previewPlaceholder->setVisible(true);
    if (showPreview) {
        ui->previewPlaceholder->setText(QString());
        if (!m_camera || !m_camera->isActive()) startCamera(true);
    } else {
        ui->previewPlaceholder->setPixmap(QPixmap());
        destroyCameraPipeline();
    }
#endif
    ui->permissionButton->setVisible(!state.cameraPermissionGranted
                                     && state.cameraAvailable);
    ui->retryButton->setVisible(state.canRetry);
    ui->importButton->setVisible(state.canImportImage);
    ui->torchButton->setVisible(state.torchSupported);
    ui->torchButton->setText(state.torchEnabled ? tr("关闭手电筒") : tr("打开手电筒"));
}
