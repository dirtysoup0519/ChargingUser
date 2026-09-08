#include "qrcodescannerwindow.h"
#include "ui_qrcodescannerwindow.h"

#include <QPushButton>
#include <QStyle>

#ifdef CHARGINGUSER_ENABLE_QT_MULTIMEDIA
#include <QCamera>
#include <QMediaDevices>
#include <QMediaCaptureSession>
#include <QVideoWidget>
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
        m_captureSession = new QMediaCaptureSession(this);
        m_videoWidget = new QVideoWidget(ui->previewPlaceholder->parentWidget());
        m_videoWidget->setGeometry(ui->previewPlaceholder->geometry());
        m_videoWidget->setVisible(false);
        m_captureSession->setCamera(m_camera);
        m_captureSession->setVideoOutput(m_videoWidget);
        connect(m_camera, &QCamera::errorOccurred, this,
                [this](QCamera::Error, const QString &) {
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
    if (m_videoWidget) {
        const bool showPreview = state.cameraPermissionGranted
            && (state.status == ScanStatus::OpeningCamera || state.status == ScanStatus::Scanning);
        m_videoWidget->setVisible(showPreview);
        if (showPreview && !m_camera->isActive())
            m_camera->start();
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
