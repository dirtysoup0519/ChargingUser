#pragma once

#include "presentation/contracts/scanviewstate.h"

#include <QWidget>

#ifdef CHARGINGUSER_ENABLE_QT_MULTIMEDIA
class QCamera;
class QHideEvent;
class QMediaCaptureSession;
class QVideoSink;
#endif

namespace Ui { class QrCodeScannerWindow; }

class QrCodeScannerWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit QrCodeScannerWindow(QWidget *parent = nullptr);
    ~QrCodeScannerWindow() override;
    void render(const ScanViewState &state);
    bool cameraAvailable() const;

signals:
    void backRequested();
    void cameraPermissionRequested();
    void scanRetryRequested();
    void imageImportRequested();
    void torchToggleRequested(bool enabled);
    void cameraStatusChanged(bool available, bool permissionGranted);

protected:
    void hideEvent(QHideEvent *event) override;

private:
    Ui::QrCodeScannerWindow *ui;
    ScanViewState m_state;
#ifdef CHARGINGUSER_ENABLE_QT_MULTIMEDIA
    QCamera *m_camera = nullptr;
    QMediaCaptureSession *m_captureSession = nullptr;
    QVideoSink *m_videoSink = nullptr;
    bool m_receivedCameraFrame = false;
    bool m_convertedCameraFrame = false;
    int m_cameraGeneration = 0;

    void createCameraPipeline();
    void destroyCameraPipeline();
    void startCamera(bool allowRestart);
#endif
};
