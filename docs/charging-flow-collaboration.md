# 充电流程 UI 与逻辑并行开发约定

日期：2026-09-07  
当前 UI 交付分支：`station-detail-navigation-ui`  
适用链路：站点详情 → 充电确认 → 充电进行 → 订单结算

2026-09-08 状态更新：非 UI 合同已推进到订单、充电会话、钱包和支付边界。UI 负责人开始实现前必须同时阅读 `docs/station-detail-business-collaboration.md` 第 12—15 节；若本文的旧阶段描述或示例名称与公共头文件冲突，以公共头文件和该更新规划为准。

本文用于 UI 负责人和逻辑负责人并行开发充电流程并安全合并。地图与站点查询继续遵守 `docs/map-navigation-collaboration.md` 和 `docs/station-detail-navigation-collaboration.md`。用户、钱包、订单和结果未知恢复继续遵守 `docs/user-module-contract.md`。

若本文与已冻结的公共头文件冲突，以公共头文件为准，并通过单独的 `contract:` 提交同时修订本文。双方不得在各自分支创建同名但字段不同的 ViewState、重复接口或临时业务类型。

## 1. 目标链路

```text
站点详情选择可用 chargerId
  → 打开充电确认页
  → 逻辑层重新核对站点、充电桩、费率、余额和用户权限
  → 用户确认开始充电
  → 逻辑层以 operationId 创建订单并启动充电
  → 成功后由 M4 打开充电进行页
  → 用户请求结束充电
  → 逻辑层以新的 operationId 停止充电并完成结算
  → M4 打开订单结算页
```

页面不根据按钮点击自行跳转。所有成功、失败、结果未知和页面去向均由业务结果与 M4 决定。

## 2. 本阶段范围

本阶段先完成“选择充电桩 → 充电确认页”的 UI 与接缝：

- 站点详情中明确选择一个可用充电桩。
- 使用稳定的 `stationId + chargerId` 请求充电确认。
- 充电确认页展示站点、充电桩、费率、钱包余额和状态反馈。
- 覆盖 Idle、Loading、Ready、Submitting、Error、ResultUnknown。
- 提供返回、刷新、去充值和确认开始充电意图。
- 提供本地排除的固定 ViewState 预览，用于 UI 验收。

本阶段不实现：

- 真实创建订单、设备控制、扣款、计费或结算。
- 在 UI 中生成 operationId、拼协议、访问网络或计算费用。
- 点击按钮后模拟启动成功或直接进入“正在充电”。
- 对 ResultUnknown 盲目重试启动操作。
- 固定选择第一个充电桩。

“充电进行”和“订单结算”列入同一协作边界，但在充电确认页验收后逐页实现。

## 3. 文件所有权

### UI 负责人修改

```text
src/presentation/contracts/chargingviewstates.h
src/presentation/pages/charging/**
ui/charging/**
styles/**
本地排除的 .local-tests/**
```

UI 负责人负责布局、控件、动态展示、输入采集、禁用原因、加载、错误、结果未知及语义意图。

### 逻辑负责人修改

```text
src/modules/charger/**       # Charger 权威状态与查询
src/modules/charging/**      # 启动、进行、停止和恢复
src/modules/order/**         # 订单与结算
对应 Mock、单元测试和网络适配
```

逻辑负责人负责设备状态、业务权限、费率、余额、订单、requestId、operationId、超时、迟到响应隔离、幂等和结果未知恢复。业务代码不得依赖 QWidget、Ui 指针、objectName 或页面编号。

### 集成阶段由唯一负责人修改

```text
src/app/**
src/flow/**
src/demo/**
ChargingUser.pro
src/presentation/presentation.pri
resources/resources.qrc
```

公共文件需要变化时，先提交独立的合同变更，再分别合并 UI 和逻辑实现。不得让两条分支同时修改同一公共接缝后依赖 Git 自动解决语义冲突。

## 4. 站点详情到充电确认的冻结接缝

### 4.1 StationDetailWindow

新增语义意图：

```cpp
void chargerSelected(const QString &chargerId);
void chargeConfirmationRequested(const QString &stationId,
                                 const QString &chargerId);
```

规则：

- 用户必须显式选择充电桩，页面不得默认取列表第一项。
- 只有详情状态为 Ready、`canCharge=true`、stationId 非空、所选 chargerId 非空且该桩 `canCharge=true` 时，确认入口才可用。
- 列表刷新后按 chargerId 保留选择；若该桩消失、离线、被占用或不可充电，立即清除选择并禁用确认入口。
- `chargerSelected` 只表达 UI 当前选择，不代表设备已被占用或订单已创建。
- `chargeConfirmationRequested` 只请求打开并加载确认页，不启动充电。
- 旧无参数 `chargeRequested()` 只作短期编译兼容，不再由新入口触发；集成完成后单独删除。

### 4.2 ChargeConfirmationWindow

展示入口：

```cpp
void render(const ChargeConfirmationViewState &state);
```

用户意图：

```cpp
void backRequested();
void confirmationRefreshRequested();
void startChargingRequested(const QString &stationId,
                            const QString &chargerId);
void rechargeRequested();
```

规则：

- `backRequested` 只表达返回，由 M4 返回站点详情并保留 stationId/chargerId 上下文。
- `confirmationRefreshRequested` 只重新查询确认快照，不复用启动充电请求。
- `startChargingRequested` 不携带 operationId；Binder/M2 在接收意图后生成唯一 operationId。
- Submitting 和 ResultUnknown 状态下确认按钮禁用，不得再次发出启动意图。
- `rechargeRequested` 只请求进入钱包充值页；是否允许充值及返回位置由 M3/M4 决定。

## 5. 首批冻结 ViewState

公共声明位置：

```text
src/presentation/contracts/chargingviewstates.h
```

状态枚举：

```cpp
enum class ChargeConfirmationStatus
{
    Idle,
    Loading,
    Ready,
    Submitting,
    Error,
    ResultUnknown
};
```

页面状态：

```cpp
struct ChargeConfirmationViewState
{
    QString stationId;
    QString chargerId;
    ChargeConfirmationStatus status;

    QString stationName;
    QString stationAddress;
    QString distanceText;

    QString chargerCode;
    QString chargerTypeText;
    QString powerText;
    QString chargerStatusText;

    QString energyPriceText;
    QString serviceFeeText;
    QString walletBalanceText;

    QString message;
    bool canStart;
    bool canRetry;
    bool canRecharge;
    QString disabledReason;
};
```

字段规则：

- stationId、chargerId 是稳定业务标识；chargerCode 仅为展示值。
- 所有 `*Text` 字段由 Binder 根据领域数据完成单位、精度和本地化，UI 不反向解析。
- 金额领域值统一使用整数分；功率使用 kW，电量使用 kWh，距离使用米，时长使用秒。
- `canStart` 是逻辑层的最终展示权限；UI 不根据余额文本、充电桩中文状态或账号类型自行推断。
- `canRetry` 只用于查询确认快照失败；启动操作 ResultUnknown 不显示普通重试。
- `disabledReason` 在按钮不可用时解释原因，不能只用灰色表达。

## 6. 逻辑服务与 Binder 要求

打开确认页时，逻辑层必须以 stationId + chargerId 获取权威快照，并核对：

- 站点与充电桩仍存在且相互归属；
- 充电桩在线、空闲且允许开始充电；
- 当前会话有效，账号状态允许新建充电；
- 当前费率版本和钱包余额；
- 当前用户没有冲突中的进行中订单；
- 服务端是否支持幂等启动及结果查询。

领域用例至少具备以下语义，实际类名可由逻辑负责人按现有架构确定：

```cpp
loadChargeConfirmation(context, stationId, chargerId);
startCharging(context, operationId, stationId, chargerId);
queryStartResult(context, operationId);
```

推荐连接：

```text
StationDetailWindow::chargeConfirmationRequested
  → ChargingUiBinder::chargeConfirmationRequested
  → M4 打开确认页并加载权威快照

ChargeConfirmationWindow::confirmationRefreshRequested
  → ChargingUiBinder::confirmationRefreshRequested

ChargeConfirmationWindow::startChargingRequested
  → ChargingUiBinder 生成 operationId
  → ChargingService::startCharging

ChargeConfirmationWindow::rechargeRequested
  → M4 打开钱包充值页

ChargingUiBinder::confirmationStateChanged
  → ChargeConfirmationWindow::render

业务启动成功
  → M4 打开充电进行页
```

Binder 不直接操作页面内部控件；页面不直接连接 Service、Socket、协议处理器或数据库。

## 7. 状态与异常规则

| 状态 | 页面行为 |
|---|---|
| Idle | 显示等待提示，确认按钮禁用 |
| Loading | 保留已有摘要并显示正在核对，确认按钮禁用 |
| Ready | 渲染权威快照；仅 canStart 时允许确认 |
| Submitting | 显示“正在启动…”，禁止重复提交和普通返回冲突操作 |
| Error | 显示明确错误；查询型失败且 canRetry 时允许重新加载 |
| ResultUnknown | 持续显示“正在确认启动结果”，禁用确认和普通重试 |

典型失败映射：

| 业务情况 | ViewState 行为 |
|---|---|
| 设备被占用/离线/故障 | canStart=false，显示明确 disabledReason |
| 余额不足 | canStart=false，可按 canRecharge 显示“去充值” |
| 账号受限 | canStart=false，显示服务端/业务层提供的原因 |
| 已有进行中订单 | canStart=false，由 M4 提供查看当前订单入口 |
| 请求超时但确定未执行 | Error，可重新加载确认快照 |
| 启动请求结果未知 | ResultUnknown，按原 operationId 查询，禁止重新启动 |

UI 不使用定时器模拟业务完成。状态刷新、轮询或推送由逻辑层控制，UI 只接收新的 ViewState。

## 8. 后续两页接缝方向

充电会话的基础合同现已冻结；结算页仍需在结算明细字段确认后提交独立合同评审。

### 充电进行页

需要展示：

- orderId、站点和 chargerId；
- 当前功率、已充电量、持续时间、当前费用；
- 连接、充电、停止中、结果未知、异常和恢复状态。

已冻结意图以 `IChargingSessionUiBinder` 为准：

```cpp
void sessionRequested(const QString &orderId); // 由流程层激活，不是页面点击
void refreshRequested();
void stopChargingRequested();
void recoverStopResultRequested();
```

页面只发出后三类无参数意图；`orderId` 由 Binder 当前状态持有。停止充电是变更操作，必须使用新的 operationId；页面不生成 operationId，也不得重复提交。

### 订单结算页

需要展示：

- orderId、充电时间、电量；
- 电费、服务费、停车费、优惠和实付金额；
- 支付/结算状态及结果未知；
- 完成和查看订单详情入口。

预期意图：

```cpp
void settlementRetryRequested();
void finishRequested();
void orderDetailsRequested(const QString &orderId);
```

金额明细来自 M3 权威结算结果，UI 不自行相加或判断支付成功。

## 9. 充电确认页关键 objectName

```text
chargeConfirmationPage
backButton
confirmationStateLabel
confirmationRetryButton
stationCard
stationNameLabel
stationAddressLabel
stationDistanceLabel
chargerCard
chargerCodeLabel
chargerTypeLabel
chargerPowerLabel
chargerStatusLabel
feeCard
energyPriceLabel
serviceFeeLabel
walletCard
walletBalanceLabel
rechargeButton
startChargingButton
```

已列出的 objectName 属于合并接缝。确需改名时先同步逻辑负责人，并以独立合同提交完成。

## 10. 并行开发与合并顺序

1. 双方从包含本文的同一提交开始开发。
2. UI 负责人只实现 ViewState 渲染、选择控件、页面意图和本地预览。
3. 逻辑负责人只实现领域服务、Mock、错误映射、幂等和 Binder 接入。
4. 如需修改首批冻结信号或 ViewState，先停止双方相关文件开发，创建单独 `contract:` 提交并共同同步。
5. 合并时先合入合同提交，再合入逻辑实现，最后合入 UI 页面；公共装配由一人完成。
6. 冲突按文件所有权处理，不用整文件覆盖另一方修改。
7. 合并后在正式 `ChargingUser.pro` 复验完整链路；独立 UI 预览不能替代正式集成验收。

## 11. 验收清单

UI 独立预览至少覆盖：

- Ready、Loading、Error、ResultUnknown；
- 设备不可用、余额不足、账号受限；
- 长站名、长地址、长禁用原因；
- 选择不同 chargerId 后确认页标识一致；
- 列表刷新导致所选桩失效；
- 快速连续点击只产生一次有效提交意图；
- 返回、重新加载和去充值只发出各自语义信号。

正式集成必须在 BitDev / Ubuntu 22.04 / Qt 6.2.4 / `qmake6` 中验证：

1. stationId/chargerId 在详情、确认、启动和订单间一致；
2. 确认页打开后设备状态变化能及时禁用提交；
3. Submitting 不创建重复订单；
4. ResultUnknown 使用同一 operationId 恢复；
5. 成功跳转由 M4 驱动；
6. 返回后恢复原站点上下文；
7. 充值返回后重新获取余额和确认快照；
8. 页面与逻辑分支合并后无重复类型、重复信号或双重连接。
