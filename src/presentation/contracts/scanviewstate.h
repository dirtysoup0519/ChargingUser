#pragma once

#include <QMetaType>
#include <QString>

enum class ScanStatus { Idle, RequestingPermission, OpeningCamera, Scanning, Validating, Error };

struct ScanViewState
{
    QString expectedStationId;
    QString expectedChargerId;
    QString chargerDisplayText;
    ScanStatus status = ScanStatus::Idle;
    QString message;
    bool cameraAvailable = false;
    bool cameraPermissionGranted = false;
    bool canRetry = false;
    bool canImportImage = true;
    bool torchSupported = false;
    bool torchEnabled = false;
};

Q_DECLARE_METATYPE(ScanStatus)
Q_DECLARE_METATYPE(ScanViewState)
