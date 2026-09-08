# 充电桩预约 UI 与逻辑协作约定

日期：2026-09-08  
适用链路：站点详情 → 预约确认 → 预约成功 → 返回站点详情

本文用于 UI 与逻辑并行开发预约功能。公共合同变更先独立提交；页面不得直接读写数据库、修改 Charger 领域状态或自行判定预约成功。

## 1. 产品交互

1. 用户点击可用充电桩整行后，该行高亮。
2. 详情页底部显示两个动作：“前往预约”和“扫码充电”。
3. “扫码充电”先打开 `QrCodeScannerWindow`；二维码经逻辑层校验成功后，再进入充电确认页。
4. “前往预约”发出 `reservationConfirmationRequested(stationId, chargerId)`，由 M4 打开预约确认页。
5. 预约确认页复用充电确认页视觉结构，将费用区改为“预约押金”和“预约时长”。
6. 预约成功由逻辑层返回结果并导航回原站点详情。对应充电桩显示“已预约”，详情顶部显示预约浮窗和剩余时间。

## 2. 冻结候选合同

```cpp
enum class ReservationConfirmationStatus {
    Idle, Loading, Ready, Submitting, Error, ResultUnknown
};

enum class ReservationCancellationStatus {
    Idle, Submitting, Error, ResultUnknown
};

struct ReservationConfirmationViewState {
    QString stationId;
    QString chargerId;
    QString stationName;
    QString stationAddress;
    QString chargerCode;
    QString chargerTypeText;
    QString powerText;
    QString depositText;
    QString durationText;
    QString depositPolicyText;
    int durationSeconds = 0;
    ReservationConfirmationStatus status = ReservationConfirmationStatus::Idle;
    QString message;
    bool canReserve = false;
    bool canRetry = false;
    QString disabledReason;
};

struct ActiveReservationView {
    QString reservationId;
    QString stationId;
    QString chargerId;
    QDateTime expiresAtUtc;
    QString remainingText;
    bool canCancel = false;
    bool canRetryCancel = false;
    ReservationCancellationStatus cancellationStatus = ReservationCancellationStatus::Idle;
    QString cancellationMessage;
    QString cancelDisabledReason;
};
```

`reservationId` 和 `expiresAtUtc` 来自逻辑层/服务端。`remainingText` 由 Binder 根据当前时间生成；UI 定时刷新只用于视觉走时，到期后必须发出刷新意图，不能自行释放充电桩。

## 3. 页面意图

```cpp
// StationDetailWindow
void reservationConfirmationRequested(const QString &stationId,
                                      const QString &chargerId);
void reservationExpiredRefreshRequested();
void cancelReservationRequested(const QString &reservationId);
void cancelReservationRetryRequested(const QString &reservationId);
void activeReservationRequested(const QString &reservationId,
                                const QString &stationId,
                                const QString &chargerId);

// ReservationConfirmationWindow
void backRequested();
void reservationRefreshRequested();
void reserveRequested(const QString &stationId,
                     const QString &chargerId,
                     int durationSeconds);
```

页面不生成 requestId、operationId 或 reservationId。Submitting/ResultUnknown 时禁止重复预约。Frozen、已有预约、桩不可用、押金不足等权限均由 Binder 输出 `canReserve/disabledReason`。

## 4. 逻辑与 Demo 数据

Demo 数据放在 `src/demo/reservation-demo-data.tmp`，经 loader 注入 Mock Reservation Service，再由 Binder 生成 ViewState。建议字段：押金整数分、可选预约时长秒数、默认时长、预约结果和到期时间策略。

取消预约 Demo 使用独立配置：

```json
"cancellation": {
  "responseDelayMs": 450,
  "outcome": "success",
  "retryOutcome": "success",
  "failureMessage": "取消预约失败，请重试",
  "resultUnknownMessage": "取消结果正在确认，请勿重复操作"
}
```

`outcome` 和 `retryOutcome` 允许 `success`、`failure`、`result_unknown`。测试失败后重试成功时设置 `outcome=failure`、`retryOutcome=success`；结果未知状态不得直接重试或释放电桩，需由逻辑层查询预约状态。

真实链路为：

```text
后端预约接口 → ReservationService → ReservationUiBinder
→ ReservationConfirmationViewState / StationDetailViewState → UI
```

预约成功后，Map/Station Binder 必须重新查询或合并权威详情，使对应 chargerId 变为 Reserved；页面不得直接改 `map-demo-data.tmp` 或本地列表冒充后端成功。

## 5. 详情页新增展示字段

`StationDetailViewState` 增加可空 `activeReservation`。存在有效预约时：

- 顶部浮窗显示已预约的充电桩编号和剩余时间；
- 对应行显示“已预约”；
- 其他不可预约动作按 Binder 权限禁用；
- 倒计时到零只发出 `reservationExpiredRefreshRequested()`，由逻辑层刷新详情。

## 6. 文件所有权与合并

- UI：`src/presentation/contracts/reservationviewstates.h`、`pages/charging/reservationconfirmationwindow.*`、`ui/charging/**`、QSS。
- 逻辑：`src/modules/reservation/**`、Mock、网络适配和测试。
- 共享：`src/app/**`、`src/flow/**`、Demo 装配、`.pri/.pro`、`resources.qrc`，由唯一集成人修改。
- 合并顺序：合同 → 逻辑 → UI → 装配。不得在两条分支创建同名不同字段类型。

## 7. 验收

- 可用桩整行选择；不可用桩不响应。
- 选择后两个动作显示且携带同一 stationId/chargerId。
- 预约页覆盖 Loading、Ready、Submitting、Error、ResultUnknown。
- 预约成功返回原详情并显示正确桩状态、预约 ID 和倒计时。
- 切页再返回、刷新和程序恢复后倒计时基于 expiresAtUtc，不从初始秒数重启。
- 到期、取消或服务端变更后由权威刷新清除浮窗并恢复桩状态。
- 预约确认页必须展示后端下发的押金和超时扣款说明，不得由 UI 推断实际扣款金额。
- 有效预约浮窗提供取消入口；取消前二次确认，提交中禁止重复点击。
- 超时、取消成功、取消失败、状态已变化和频繁预约/取消限制均须明确提示。
- 一个账号同一时间只能有一个有效预约；限制原因由 Binder 输出，服务端仍须再次校验。
- 取消频率、冷却时间、是否退还押金均由后端规则决定，UI 只展示结果。
- 存在有效预约时，详情页底部用可点击的“查看已有预约”替换“前往预约”。点击后由 Binder 打开预约所属站点，并定位、高亮对应 chargerId；不得依赖当前列表仍停留在原站点。
- 当用户已经选中本人预约的 chargerId 时，同一底部按钮改为“取消预约”；选择其他电桩时恢复为“查看已有预约”。取消中的按钮显示“取消中…”，可重试失败显示“重试取消”。
- 点击“查看已有预约”必须先弹出确认提示，明确将前往预约所属站点和充电桩；仅在用户确认后发出 `activeReservationRequested`。
- 首页搜索栏下方、地图上方显示有效预约卡，包含预约电桩、所属站点和基于 `expiresAtUtc` 的倒计时；无预约时隐藏并恢复地图高度。整卡点击前先确认，再复用 `activeReservationRequested` 跳转。
- 取消预约覆盖 Idle、Submitting、Error、ResultUnknown。Submitting 和 ResultUnknown 禁止重复提交；Error 仅在 `canRetryCancel` 为真时显示“重试取消”。

## 8. 扫码充电页面与职责

“扫码充电”先进入独立扫码页，不直接启动充电。扫码页负责摄像头预览、扫描框、权限/设备/识别状态、手电筒和从相册选择入口；它不解析业务字段、不判断电桩可用性，也不调用启动充电接口。

推荐真实实现使用 Qt 6 Multimedia：`QMediaDevices` 枚举摄像头，`QCamera` 与 `QMediaCaptureSession` 管理采集，`QVideoSink` 接收帧，ZXing-C++ 解析二维码。启用后由集成人添加 `QT += multimedia`；只有使用 `QVideoWidget` 时才增加 `multimediawidgets`。虚拟机无摄像头时必须保留“从相册选择”路径，确保 UI 和二维码校验流程可测试。

```cpp
enum class ScanStatus {
    Idle, RequestingPermission, OpeningCamera, Scanning, Validating, Error
};

struct ScanViewState {
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
```

页面只发出意图：

```cpp
void backRequested();
void cameraPermissionRequested();
void scanRetryRequested();
void imageImportRequested();
void torchToggleRequested(bool enabled);
```

Scanner 模块识别后向 Binder 返回原始结果，建议业务合同为：

```cpp
struct ScanResult {
    QString rawText;
    QString stationId;
    QString chargerId;
    QString token;
};
```

Binder 必须校验二维码格式、站点、电桩、一次性令牌、令牌有效期以及当前电桩状态。只有 `stationId` 和 `chargerId` 与扫码页的 expected 值一致且后端验证成功，才导航到充电确认页。失败、过期、不匹配均留在扫码页显示可恢复错误；`Validating` 期间禁止重复处理同一码。UI 不保存摄像头帧和令牌，不在日志输出完整二维码内容。

文件所有权补充：UI 负责 `scanviewstate.h`、`qrcodescannerwindow.*` 和对应 `.ui`/QSS；逻辑负责 `modules/scanner/**`、ZXing 适配、权限处理和扫码结果验证；`.pri/.pro` 与 Demo 装配仍由唯一集成人修改。

扫码验收覆盖：无摄像头、权限未授予、权限拒绝、打开中、扫描中、校验中、二维码不匹配、二维码过期、相册导入、返回详情，以及本人预约电桩扫码。扫码成功只能进入确认页，确认页再次提交后才可启动充电。
## 9. 2026-09-08 UI 交付快照

- 已实现：动态电桩整行选择、预约确认页、本人预约桩高亮、预约倒计时、首页预约卡、查看已有预约、取消预约及异常状态展示、扫码页面和充电确认页面。
- 已验收按钮状态：无预约时“前往预约”；有预约且选择其他桩时“查看已有预约”；选择本人预约桩时“取消预约”；取消提交中为“取消中…”；失败可重试时为“重试取消”。
- 已验收已有预约入口：详情页和首页均先确认，再通过 `stationDetailsRequested(stationId)` 进入预约所属站点，选择、滚动并高亮 chargerId；不能仅切换页面控件，否则 Binder 返回状态会错误。
- Demo fixture 当前便于验收的参数为预约 30 秒、取消冷却 3 秒。正式环境的预约时长、冷却截止时间、押金和取消规则必须来自服务端。
- 取消 Demo 的 `cancellation.outcome/retryOutcome` 支持 `success`、`failure`、`result_unknown`。结果未知时保留预约与电桩状态并禁止重复取消，等待权威查询。
- 扫码页当前完成 UI 和意图接口；真实摄像头、相册选择、ZXing-C++ 解码、令牌验证及启动充电由逻辑负责人接入。
- 用户已完成本轮 UI 验收。合并前需在 Qt 6.2.4 Demo 中重新 qmake 并完整构建，确保新增页面的 uic/moc/qrc 均进入产物。
