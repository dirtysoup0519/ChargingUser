#pragma once

#include "presentation/contracts/scanviewstate.h"

#include <QWidget>

namespace Ui { class QrCodeScannerWindow; }

class QrCodeScannerWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit QrCodeScannerWindow(QWidget *parent = nullptr);
    ~QrCodeScannerWindow() override;
    void render(const ScanViewState &state);

signals:
    void backRequested();
    void cameraPermissionRequested();
    void scanRetryRequested();
    void imageImportRequested();
    void torchToggleRequested(bool enabled);

private:
    Ui::QrCodeScannerWindow *ui;
    ScanViewState m_state;
};
