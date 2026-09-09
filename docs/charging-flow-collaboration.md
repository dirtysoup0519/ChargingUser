# 充电流程 UI 与逻辑并行开发约定

日期：2026-09-07  
当前 UI 交付分支：`station-detail-navigation-ui`  
适用链路：站点详情 → 充电确认 → 充电进行 → 订单结算

2026-09-08 状态更新：充电确认、扫码、并行充电会话、预约、结算、统一支付和订单列表 UI 已完成本地 Demo 接线。用户已验收预约支付 → 已预约 → 扫码 → 充电中、结束充电 → 待结算 → 支付 → 已完成、取消预约 → 押金已退回，以及首页/站点详情可用桩数量同步。正式历史订单分页、预约订单查询、订单详情 Binder、退款与支付结果恢复仍待逻辑负责人接入。若本文旧阶段描述或示例名称与公共头文件冲突，以公共头文件和本状态更新为准。

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
- 当前账号是否仍允许新建并发充电会话；已有进行中订单本身不构成冲突，具体并发上限及禁用原因由逻辑层给出；
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

启动成功还必须以同一 `stationId + chargerId + orderId` 更新或重新查询 Charger 权威状态。站点详情随后通过既有 `stationDetailStateChanged` 获得“充电中”状态；首页可用桩数量也应由重新查询或服务端推送更新。充电进行页不得直接查找并修改站点详情控件。

```text
startCharging 成功
  → Charging/Order 服务确认 orderId
  → Charger 状态缓存失效或接收 chargerStateChanged(stationId, chargerId)
  → IChargerService::queryStationDetail / queryStations 返回权威快照
  → MapUiBinder 重新生成 StationDetailViewState / HomeMapViewState
  → charger.statusText="充电中"，canCharge=false，并更新站点 availableCount
```

停止并结算成功后执行相同刷新链路，使充电桩从“充电中”恢复为服务端返回的空闲、离线或故障状态。启动失败或 ResultUnknown 时不得提前显示为空闲；ResultUnknown 应持续查询原 operationId，并在确定结果后再刷新 Charger 状态。

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
| 已有进行中订单 | 不直接禁用扫码充电；逻辑层按账号权限、设备状态和并发上限决定 canStart，达到上限时提供明确 disabledReason |
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

#### 充电进行页视觉与多会话补充合同（2026-09-08）

页面按 `file1/06充电进行.png` 的信息层级实现：顶部居中标题；主体为动态圆环、进度百分比和当前功率；圆环下方并列展示已充电量、已充时长和当前费用；中部展示当前充电会话选择卡；底部提供“结束充电”。现有底部一级导航保持不变。

同一账号允许通过多次扫码建立多笔并行充电会话。页面不能把“已有进行中订单”当作唯一会话，也不能固定显示最近一笔。中部充电桩卡片必须作为会话选择器：

- 收起时显示当前会话的站点名、桩号、充电类型、额定功率和业务状态，并提供明确的下拉箭头。
- 点击卡片任意非操作区域展开下拉面板；再次点击、选中一项或点击面板外区域时收起。
- 下拉项以稳定 `orderId` 标识一笔会话，展示站点名、桩号、状态、当前功率和已充时长，不能使用列表下标作为身份。
- 会话数量不固定；超过面板可视高度后只滚动下拉列表，页面主体位置不跳动。
- 切换会话只发出选择意图并切换当前展示，不停止、不刷新创建、不改变其他会话状态。
- 当前会话结束或消失时，由 Binder 返回新的选中项；仍有进行中会话时优先选择其一，没有会话时进入空态或由 M4 打开结算页。

为避免破坏已经冻结的单会话合同，`ChargingSessionViewState` 继续描述一笔被选中的会话；新增的会话集合使用独立合同，不在 UI 分支私自修改 `IChargingSessionUiBinder`：

```cpp
struct ChargingSessionSummaryView
{
    QString orderId;
    QString stationName;
    QString chargerCode;
    QString chargerTypeText;
    QString ratedPowerText;
    QString currentPowerText;
    QString durationText;
    ChargingSessionStatus status;
};

struct ChargingSessionCollectionViewState
{
    QList<ChargingSessionSummaryView> sessions;
    QString selectedOrderId;
    QString message;
    bool loading;
    bool canRefresh;
};
```

以下多会话语义已冻结在公共 Binder 与订单服务头文件中，逻辑负责人实现服务端适配时保持函数签名不变：

```cpp
void activeSessionsRequested();
void activeSessionSelected(const QString &orderId);
void activeSessionsStateChanged(const ChargingSessionCollectionViewState &state);
```

订单服务集合接缝：

```cpp
void IOrderService::queryActiveOrders(const RequestContext &context);
void IOrderService::activeOrdersReady(const RequestContext &context,
                                      const QVector<ChargingOrder> &orders);
```

旧 `queryActiveOrder()` 暂时保留供单会话调用方兼容；新充电页必须使用集合接口。服务端适配器负责把真实订单列表映射为 `QVector<ChargingOrder>`，Binder 再映射为页面 ViewState。接入真实服务端时不得修改页面信号或让页面解析 JSON。

`activeSessionSelected` 成功后，Binder 更新当前 `ChargingSessionViewState`。页面的 `refreshRequested()`、`stopChargingRequested()` 和 `recoverStopResultRequested()` 始终作用于 Binder 当前持有的 `orderId`；Stopping 或 ResultUnknown 期间切换到其他会话，不得清除原会话的 operationId 或恢复任务。

动态圆环遵守以下规则：

- 圆环百分比必须来自逻辑层提供的权威 `progressPercent`，取值限制为 0—100；车辆或设备不能提供进度时显示不定进度动画和“充电中”，不得用已充时长推算百分比。
- 收到新的权威百分比后，UI 使用 300—500 ms 的属性动画从当前显示值平滑过渡到新值；首次打开可从 0 动画到当前值，切换会话时直接从该会话上次显示值过渡。
- 充电状态下圆环保留缓慢流动或高光动画，让用户能辨认页面仍在运行；Loading、Stopping、ResultUnknown 和 Error 使用各自状态视觉，不能继续显示为正常增长。
- `energyText`、`amountText`、当前功率和百分比均由 Binder 提供，UI 不使用定时器自行累加。已充时长可由逻辑层定期推送，或在双方确认后以权威开始时间做纯展示计时。
- 后台刷新或推送可以改变进度；迟到响应必须按 orderId/requestId 隔离，不能把会话 A 的数据画到当前会话 B。

单会话状态需在公共合同评审时补足以下展示字段；在合同提交合入前，UI 预览可使用本地夹具，但不得把重复类型提交进正式工程：

```cpp
int progressPercent;          // -1 表示设备未提供
QString currentPowerText;
QString durationText;
QString chargerTypeText;
QString ratedPowerText;
```

多会话异常规则：

| 情况 | 页面行为 |
|---|---|
| 无进行中会话 | 展示空态和扫码入口；不显示伪造的 0% 会话 |
| 集合加载失败 | 保留已有列表并显示可重试提示，不能清空当前会话造成闪烁 |
| 当前会话刷新失败 | 只标记当前会话错误；其他会话仍可选择 |
| 当前会话已结束 | 禁止再次结束；由 M4 打开对应结算或选择下一笔进行中会话 |
| 一笔会话停止中 | 该项显示“停止中”并禁止重复提交，其他会话仍可查看 |
| 停止结果未知 | 该项持续显示“正在确认结果”，只允许按原 operationId 恢复 |
| 达到并发上限 | 扫码确认页显示逻辑层提供的原因；现有会话选择和停止不受影响 |

充电进行页建议固定以下关键 objectName，待公共合同提交后冻结：

```text
chargingSessionPage
chargingSessionTitle
chargingProgressRing
chargingProgressLabel
currentPowerLabel
energyValueLabel
durationValueLabel
amountValueLabel
sessionSelectorButton
sessionSelectorPopup
sessionListView
sessionStateLabel
sessionRefreshButton
stopChargingButton
```

UI 独立预览至少覆盖：1、2、5、20 笔并行会话；列表展开和滚动；快速切换；会话结束后重新选择；0%、68%、100% 与未知进度；Loading、Charging、Stopping、ResultUnknown、Ended、Error；长站名和长桩号；切换时数据不串行；结束按钮只影响当前 orderId。

无活动订单时使用独立空状态，不展示空白圆环、`--` 指标或不可用的会话选择卡。页面显示
充电图形、“还没有充电任务”、扫码说明和“扫码开始充电”主按钮；扫码启动成功后再切换
到圆环、实时指标和多会话选择。Loading 或 Error 仍由 ViewState 的 message、刷新能力和
按钮状态驱动，空状态不得掩盖真实错误。

Demo 与正式实现都必须遵守站点主数据单一来源：站点名、地址、stationId、chargerId、充电类型、额定功率和 Charger 状态来自 `IChargerService`（Demo 对应 `map-demo-data.tmp`）。充电会话数据源只提供 orderId、实时功率、累计电量、累计费用、进度及时间等会话遥测，不得重复创建站点或电桩记录。扫码结果只返回已经存在的 stationId + chargerId，再查询 Charger 权威详情；禁止生成 `demo-station-*` 等脱离站点数据源的业务标识。

### 订单结算页

需要展示：

- 充电费用；
- 充电时间；
- 支付方式；
- 站点与充电桩信息；
- 支付/结算状态及结果未知。

已实现意图：

```cpp
void paymentRequested(const QString &orderId);
void backRequested();
```

总充电费用来自 M3 权威结算结果，UI 不拆分服务费、停车费、优惠，也不自行计算或判断支付成功。

### 订单结算与统一支付页面接口（2026-09-08）

结束充电确认成功后，流程协调层必须先查询权威订单明细并渲染
`SettlementViewState`，再打开 `SettlementWindow`。页面按当前产品方案只展示充电
费用、充电时间、支付方式和充电桩信息，不展示服务费、停车费、优惠等拆分费用；
页面不得自行计算或解析金额。结算页主按钮固定为“前往支付”，发出
`paymentRequested(orderId)`。

充电订单和预约押金共用 `PaymentWindow`。`PaymentViewState::purpose` 区分
`ChargingSettlement` 与 `Reservation`，`businessId` 对应服务端订单号或预约支付
业务号。页面展示待支付金额和钱包余额，并只发出 `payRequested(businessId,
purpose)`、`rechargeRequested()` 和 `backRequested()`。正式接入分别调用
`IWalletService::queryWallet`、`payOrder` 及结果查询接口；提交中和结果未知时禁止
重复支付。支付成功由协调层刷新钱包、订单、站点和充电桩状态，然后返回首页。

支付页视觉于 2026-09-09 与订单结算页统一：顶部使用无胶囊背景的居中标题，金额作为
页面主视觉，站点/桩说明紧随其后；支付信息卡展示订单类型、钱包支付、当前余额和支付后
余额，底部固定主按钮携带本次金额。`balanceAfterPaymentText` 必须由 Binder 使用整数分
生成，页面不得解析 `amountText` 或 `balanceText`。余额不足时主按钮语义改为前往充值；
Submitting 显示“正在支付…”，ResultUnknown 只显示“查询支付结果”。

支付页必须覆盖余额不足、提交中、失败和结果未知：余额不足时禁用支付并保留充值入口；
提交中立即禁用支付和充值；明确失败且确认未扣款后才允许重试；结果未知只允许按原
businessId/operationId 查询结果，严禁重新创建支付。页面禁用按钮，协调层仍需使用
操作锁防止双击、快捷键或重复信号造成二次提交。Demo 可通过
`payment-demo-data.tmp` 的 `walletBalanceText` 和 `outcome` 切换这些场景。

预约确认按钮显示“确认预约并支付”。支付成功前只保存支付上下文，不创建预约订单，
也不提前占用充电桩；支付成功后由预约服务创建并返回权威预约及到期时间，随后才将
预约订单加入“我的订单”并刷新首页预约提示。预约订单不存在“待支付”状态。
Demo 的余额、结算明细、延迟和支付结果来自 `payment-demo-data.tmp`，该文件仅是
服务端替身，不得成为正式页面的数据源。

### 我的订单列表

“我的订单”统一展示充电订单和预约订单，并提供“全部 / 充电订单 / 预约订单”筛选。
两类业务记录仍由各自服务保存，通过 `OrderListViewState` 汇总展示。支付记录只关联
业务订单，不作为第三类订单出现在列表中。

充电订单可处于充电中、待支付、已完成、已取消或结果待确认；待支付项提供“继续支付”
并先回到结算明细。预约订单只在支付成功且预约创建成功后产生，因此不存在“待支付”
状态；可展示已预约、已使用、已取消、已超时和退款处理中。页面通过
`orderActionRequested(businessId, type, action)` 发出意图，不根据状态文字决定跳转。

已预约但尚未扫码的订单被点击时，协调层先弹出“是否前往扫码充电”；确认后使用该
预约的 stationId/chargerId 打开扫码页。扫码启动成功后预约订单状态变为“充电中”并
关联新建的充电订单；充电结束后变为“已使用”。取消预约且押金退款确认成功后显示
“已退回”。退款结果未确认时必须保持独立的“退款处理中”状态。

### 正式订单详情页接口（2026-09-09）

订单列表中的每一笔充电订单和预约订单都提供“查看详情”，并通过既有
`orderActionRequested(businessId, type, ViewDetails)` 打开应用内详情页，不再使用
`QMessageBox` 拼接订单文字。待支付、充电中、已预约等业务动作继续作为独立按钮，
不能用“查看详情”替代。

详情页使用 `OrderDetailViewState`，字段包含 businessId、relatedBusinessId、
stationId、chargerId、订单类型、站点、桩号、创建时间、时长、电量、金额、支付方式、
状态、提示和语义动作。金额和时间均由 Binder 格式化，页面不计算费用，也不根据状态
文字推断按钮行为。当前 Demo 从 `OrderListItemView` 映射同一份运行时订单；正式接入时
应先用 businessId 查询权威订单详情，再 render 页面。

视觉与订单结算页保持一致：顶部居中标题、完成插画、订单类型、状态标签、大号金额、
订单信息卡、支付信息卡和底部操作区。预约订单隐藏无意义的充电电量；长订单号、站名、
桩号和状态必须自动换行或在信息行内收缩。详情页返回“我的订单”并保留原筛选和滚动
上下文。正式 Binder 至少覆盖 Loading、Ready、Error、ResultUnknown 和数据已更新状态。

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

## 12. 当前 Demo 数据接口总表

所有 `.tmp` 文件都是正式服务端的本地替身。页面不得直接打开、解析或改写 tmp；
当前数据经 Fixture Loader 或 `UserDemoController` 转成 ViewState 后交给页面渲染。
正式接入时应保留页面信号和 ViewState，替换 Demo Controller 内的数据来源。

| tmp 资源 | 当前负责的数据 | 当前读取位置 | 正式服务端对应能力 |
| --- | --- | --- | --- |
| `map-demo-data.tmp` | 站点、地址、经纬度、距离、stationId、桩列表、chargerId、类型、额定功率、电价、在线和可启动状态 | `loadMapDemoFixture()` → `UserDemoController` | `IChargerService` 的站点列表、站点详情和设备状态快照 |
| `charge-demo-data.tmp` | 充电确认页的钱包展示、服务费展示和充值能力等 Demo 辅助字段 | `loadChargeConfirmationDemo()` | 钱包查询、计费规则快照；正式金额必须使用整数分 |
| `charging-session-demo-data.tmp` | 扫码启动成功后创建会话所用的遥测模板：功率、电量、时长、费用和进度 | `loadChargingSessionDemo()` | 充电订单详情、实时遥测或轮询结果 |
| `reservation-demo-data.tmp` | 预约押金、时长、超时说明、取消冷却、响应延迟和异常结果 | `loadReservationDemoFixture()` | 预约报价、创建、取消、退款和结果查询 |
| `payment-demo-data.tmp` | 钱包余额、支付延迟、支付结果场景和结算金额 | 当前由 `UserDemoController` 解析 | `IWalletService` 的余额、支付、充值和操作结果查询 |
| `order-list-demo-data.tmp` | 仅用于可选的历史订单种子；当前保持空数组，便于从零回归 | 当前由 `UserDemoController` 解析 | 订单聚合查询或充电/预约订单分页接口 |
| `user-demo-data.tmp` | Demo 账号、登录资料与新用户默认资料 | Demo 用户加载逻辑 | 用户、认证和资料服务 |

`map-demo-data.tmp` 是站点与充电桩主数据的唯一来源。其他 tmp 只能通过
`stationId + chargerId` 引用已有设备，不得复制站名、电桩列表或创建第二套站点数据。
`charging-session-demo-data.tmp` 只是遥测模板，应用启动时不得据此自动产生充电中订单；
模板只能在扫码校验并启动成功后实例化。

当前 `payment-demo-data.tmp` 和 `order-list-demo-data.tmp` 仍由
`UserDemoController` 直接解析，这是 Demo 阶段的临时实现。后端接入时应移动到 Mock/
Repository/Service 层，页面和 ViewState 不随之改变。所有 tmp 受 `.gitignore` 影响；
需要共享无密钥测试数据时必须显式强制加入 Git，地图 key、本地账号令牌和
`*.local.json` 禁止提交。

## 13. 页面意图与跳转实现

页面只发出语义信号，`UserDemoController` 当前承担流程协调；正式版本由 M4/Binder
承担同一职责。任何页面不得通过按钮文字、状态标签文字或金额字符串决定跳转。

```text
首页站点行“查看详情”
  → stationId 查询详情
  → 充电站详情

充电站详情选择 chargerId
  ├─ 空闲桩：显示“前往预约”与“扫码充电”
  ├─ 当前账号已预约该桩：显示“取消预约”与“前往扫码充电”
  └─ 当前账号已预约其他桩：显示“查看已有预约”并二次确认后跳到对应站点/桩

前往预约
  → 预约确认页
  → “确认预约并支付”
  → PaymentWindow(purpose=Reservation)
  → 支付成功后创建预约
  → 刷新订单、首页、站点详情和预约浮窗
  → 首页

扫码充电
  → 二维码页（Demo 用导入图片代替摄像头）
  → 校验扫码得到的 stationId + chargerId
  → 充电确认页
  → startChargingRequested(stationId, chargerId)
  → 创建/获取 charging orderId
  → 充电进行页

充电进行页“结束充电”
  → stopCharging(orderId, operationId)
  → 确认结束后释放设备占用并生成待结算订单
  → 订单结算页
  → “前往支付”
  → PaymentWindow(purpose=ChargingSettlement)
  → 支付成功后订单变为已完成并刷新钱包
  → 首页

我的 → 我的订单
  ├─ 充电中：返回对应充电会话
  ├─ 待结算：打开订单结算页，再进入统一支付页
  ├─ 已预约：确认“是否前往扫码充电”，确认后打开扫码页
  └─ 已完成/已退回：打开订单详情（当前 Demo 为简要弹窗，正式页面仍待实现）
```

返回逻辑必须恢复来源上下文：扫码页返回站点详情或充电页；充值页返回原支付页并重新
查询余额；地图导航返回原站点详情；二级页面统一由页面栈或流程协调层管理，不允许
临时创建无父对象的顶层选择窗口。

## 14. 预约、充电、订单与支付状态合同

### 14.1 预约状态

推荐领域状态为 `QuoteReady → PaymentSubmitting → Reserved → Charging → Used`，
取消分支为 `Reserved → CancelSubmitting → RefundProcessing → Returned`，异常分支包含
`Expired`、`CancelCooldown`、`ResultUnknown` 和明确 `Error`。

- 支付成功前没有预约订单，也不得占用充电桩；预约订单不存在“待支付”。
- 支付成功后再调用创建预约；创建失败时不能伪造已预约，需按后端合同退款或恢复。
- 同一账号同时只能有一个有效预约；已有预约时其他桩入口变为“查看已有预约”。
- 已预约桩可取消或扫码；扫码启动成功后预约状态改为 `Charging`，不得重复占用数量。
- 取消成功且退款确认后状态为“已退回”；退款未知时保持“退款处理中”。
- 超时后显示预约失效及押金处理结果，不能仅靠本地倒计时决定扣款。
- 取消冷却截止时间由服务端返回；Demo 的 `cancellationCooldownSeconds` 只用于测试。

### 14.2 充电状态

`ChargingSessionStatus` 已定义 `Idle / Loading / Charging / Stopping /
ResultUnknown / Ended / Error`。每个会话使用不可变 `orderId` 作为主键，多次扫码会产生
多个独立 orderId。切换卡片只改变当前展示 orderId，结束按钮只作用于当前会话。

扫码结果必须映射到 `map-demo-data.tmp` 或服务端已有的 stationId/chargerId。设备不可用、
离线、被他人占用或扫码标识不匹配时，停留在确认链路并展示后端原因。启动成功后才把
设备标为使用中；结束确认成功后才释放为可用。停止结果未知时保持占用并查询原
operationId，不能先释放设备或重复发送停止请求。

### 14.3 订单状态

充电订单至少支持 `Charging / PendingSettlement / PaymentProcessing / Completed /
Cancelled / ResultUnknown`。预约订单至少支持 `Reserved / Charging / Used /
Cancelled / Expired / RefundProcessing / Returned`。支付记录只关联业务订单，不作为
“我的订单”的第三种业务订单展示。

`OrderListViewState` 是列表展示合同，动作由 `OrderListAction` 明确表达：
`ContinuePayment`、`ViewCharging`、`StartReservedCharging`、`ViewDetails`。正式后端应
补充历史订单分页能力，例如 `queryOrders(context, filter, cursor, pageSize)`，返回稳定
排序的数据、`nextCursor` 和 `hasMore`；这是待实现接口建议，并非当前
`IOrderService` 已存在的方法。

### 14.4 支付状态

`PaymentViewStatus` 已定义 `Ready / Submitting / Success / Error / ResultUnknown`。
`PaymentPurpose` 区分充电结算与预约押金，但两者共用同一个 PaymentWindow。

- 金额使用整数分传输、比较和扣减；格式化为“¥xx.xx”只发生在 ViewState/Binder。
- 每次支付携带业务号和幂等键，服务端返回不可变 operationId。
- `Submitting` 时页面禁用支付和充值，协调层同时持有操作锁，防止双击与重复信号。
- 明确失败且确认未扣款后才能重试；余额不足保留充值入口。
- `ResultUnknown` 只能查询原 operationId，不能创建新支付。
- 支付成功以服务端结果为准，随后重新查询钱包和业务订单；UI 不自行扣余额。
- 当前 Demo 会在内存中扣减余额并模拟不同 outcome，仅用于演示，不是正式账务逻辑。

## 15. 充电计量与金额计算要求

当前 Demo 不执行真实充电计算。`charging-session-demo-data.tmp` 中的
`currentPowerText`、`energyText`、`durationText`、`amountText` 和 `progressPercent`
是可替换的展示模板。正式接入必须使用原始数值 DTO，不允许从这些格式化文字反解析。

后端或领域层必须遵守以下规则：

1. 累计电量以电桩电表读数或服务端累计值为准，不能用 UI 定时器把瞬时功率乘时间。
2. 已充时长由服务端 `startedAt`、`endedAt` 和服务端时间计算；UI 可在两次刷新间平滑递增，下一次快照必须校准。
3. 当前功率是遥测值，可缺省；额定功率来自充电桩主数据，两者不能混用。
4. 计费使用启动时保存的价格快照/规则版本，避免会话中途调价改变已发生费用。
5. 累计费用和最终应付金额由服务端以整数分返回；结算页不得把电量、电价或其他文字自行相乘相加。
6. 最终结算以停止充电后的权威订单明细为准，即使最后一帧实时费用与结算金额有短暂差异。
7. `progressPercent` 只有在可靠 SOC/目标进度存在时才提供；未知用 `-1`，圆环显示不定进度动画，禁止虚构电池百分比。
8. 每帧遥测必须携带 orderId 和时间戳/版本号；丢弃旧于当前版本的数据，防止多会话切换串值。

建议正式会话快照至少包含：`orderId`、`stationId`、`chargerId`、`status`、
`startedAt`、`serverTime`、`currentPowerW`、`energyWh`、`amountCents`、
`progressPercent`、`pricingSnapshotId` 和 `version`。ViewState 再把 W/Wh/分和时间格式化为
页面文字。

## 16. 可用桩数量与跨页面同步

首页可用数、站点详情每个桩的状态、预约浮窗、充电页和订单页必须来自同一份业务快照。
当前 Demo 用 `stationId + chargerId` 的运行时占用集合叠加到
`map-demo-data.tmp`：有效预约和充电中都计为不可用；预约转充电只改变占用原因，不重复
扣减；取消预约、预约超时释放或结束充电确认成功后恢复可用。

正式接入后，预约成功、预约取消、启动充电、停止充电和支付完成都应触发相关查询：

- 重新获取站点详情或应用服务端推送的设备版本；
- 重新获取首页当前区域站点摘要；
- 重新获取有效预约与订单列表；
- 涉及资金时重新获取钱包余额。

UI 禁止通过“可用 3/5”文字做减一或加一，也不能根据本地按钮点击直接认定成功。
若服务端返回的设备版本旧于当前版本，应忽略旧快照。

## 17. 接入顺序与回归要求

伙伴接入真实服务端时按以下顺序替换，可减少 UI 与业务同时变化造成的冲突：

1. 保留 presentation contracts 和页面信号，先用真实 `IChargerService` 替换地图/站点 tmp。
2. 接入扫码解析与启动充电，确认 stationId/chargerId/orderId 全链一致。
3. 接入充电会话查询或推送、停止操作及 operationId 结果恢复。
4. 接入结算订单、钱包余额、支付幂等和结果查询。
5. 接入预约报价、支付后创建、取消、退款、超时与冷却截止时间。
6. 接入订单聚合分页，并把当前简要弹窗替换为正式订单详情页。
7. 删除 Controller 中对应 tmp 解析和内存扣款/占用模拟，但保留 Mock 供离线回归。

每次替换后至少回归：预约支付→已预约→扫码→充电中；结束充电→待结算→支付→已完成；
取消预约→押金已退回；余额不足、失败、处理中、结果未知与重复点击；多次扫码形成多个会话；
首页和站点详情可用数一致；冷启动不出现虚假充电订单；同一 operationId 恢复不造成二次
扣款、二次预约或二次停止。

## 18. 登录前法律内容页（2026-09-09）

登录页的《用户协议》和《隐私政策》是两个独立的应用内链接。点击后在登录窗口内部打开
对应内容页，页面提供居中标题、可滚动正文和返回按钮；返回后必须保留手机号输入、协议
勾选和错误提示，不创建独立顶层窗口，也不自动改变同意状态。

当前正文是一段用于 UI 演示和接口验收的产品说明。正式发布前应由项目负责人或法律
人员提供完整版本、版本号、生效日期、运营主体、联系方式、数据保存期限、撤回授权及
账号注销办法。服务端后续可返回文档版本与正文，但页面只负责展示；用户实际同意的版本
应由业务层记录，不能只依赖本地复选框。

## 19. “我的”页面辅助入口（2026-09-09）

“我的”菜单中的“常用充电站”“帮助与反馈”“关于智充”均为应用内二级页，不创建独立
窗口。三个入口分别发出 `frequentStationsRequested()`、`feedbackRequested()` 和
`aboutRequested()`，返回后回到“我的”主页。

常用充电站不得维护固定站点名单。当前 Demo 从已有 `OrderListViewState::orders` 提取
stationId、stationName 和 createdAtText，以 stationId 聚合订单次数，按次数降序生成
`FrequentStationsViewState`；同名站点不得在 stationId 存在时合并。每个站点卡展示累计
订单数和最近使用时间，点击“查看站点”发出 stationId，并复用站点详情查询与跳转。
正式接入可由订单聚合 Binder 基于完整历史订单生成，或由服务端直接返回常用站点统计；
无论采用哪种方式，页面不得读取 tmp、数据库或自行查询订单。

帮助与反馈、关于智充当前使用可替换的演示正文。反馈页后续如增加提交功能，应新增
feedback ViewState 和语义意图，覆盖提交中、失败、结果未知及重复点击；关于页后续展示
版本、版权和服务条款时由 Binder 提供内容，页面只负责渲染。

## 20. 双登录方式、首次资料完善与密码修改（2026-09-09）

登录页支持“手机号登录”和“用户名密码登录”两种模式，切换入口位于账号输入区左下方。
手机号模式继续发出既有 `loginRequested(phone)`；用户名模式发出
`usernamePasswordLoginRequested(username, password)`。两种模式都必须先勾选用户协议，
密码输入使用 Password echo，不得在日志或 ViewState 中回显明文。

首次资料完善通过 `ProfileEditMode` 区分：

- `PhoneFirstSetup`：手机号只读，用户填写昵称、新密码和确认密码；两次密码一致且满足
  最低长度后发出 `profileCompletionRequested(nickname, phone, newPassword)`。
- `UsernameFirstSetup`：用户名作为只读用户名/昵称展示，必须填写并绑定 11 位手机号；
  原登录密码沿用，不要求用户再次设置密码。
- `ExistingProfile`：保留昵称编辑和只读手机号，显示“修改密码”入口，不显示首次设置字段。

已有用户修改密码的 UI 顺序固定为：在资料编辑页弹出原密码输入框 → 提交原密码 → 服务端
验证成功 → 弹出明确确认提示 → 进入与主应用同尺寸的新密码/确认密码页 → 提交修改 → 显示
结果。取消验证弹窗时停留在资料编辑页，验证失败不得缩放、隐藏或替换当前页面。页面不得比较原密码；
当前 Demo Controller 使用 `user-demo-data.tmp` 的 password 字段模拟服务端验证。正式接入
必须新增用户名密码登录、绑定手机号、设置初始密码、验证原密码和修改密码接口；密码只
在请求体内短暂传递，禁止持久化明文。绑定手机号和密码修改属于变更操作，需要 requestId、
operationId、重复提交保护及 ResultUnknown 恢复。服务端必须校验用户名和手机号唯一性，
客户端的长度检查仅用于即时提示。

登录页顶部插画采用 Hero 背景结构：新能源汽车与充电桩图片覆盖上半部分视觉区域，“智充”
和品牌标语叠放在图片左上方；图片不再作为品牌文字与表单之间的普通独立 QLabel。背景
保持原始宽高比并使用平滑缩放，窗口裁切时不得拉伸车辆或充电桩。登录方式切换控件按
账号输入框的实际布局位置插入，禁止依赖固定 layout index，以免 Hero 或提示控件调整后
导致用户名、密码输入框顺序错乱。

用户协议与隐私政策共用 `LegalDocumentPage`。该页面作为登录窗口内的全尺寸不透明覆盖层，
必须同步登录窗口 geometry、主动绘制页面背景并位于登录控件上方，避免 Hero 品牌文字从
标题栏或卡片空白处透出。协议返回只关闭覆盖层，不重建登录窗口，也不清空用户输入。

## 21. 最终 UI 验收基线（2026-09-09）

本轮用户已确认现有页面整体无问题。正式联调应保留以下已经验收的表现和跳转：

- 登录页采用上半区 Hero 背景，品牌文字叠加；支持手机号与用户名密码两种入口，未同意
  协议时阻止登录并提示。
- 手机号首次登录要求设置并确认密码；用户名首次登录保留用户名并强制绑定手机号。
- 编辑资料中的密码修改先使用模态输入框验证原密码，成功后进入完整尺寸的设置密码页。
- 用户协议、隐私政策、常用充电站、帮助与反馈、关于智充均为应用内页面并可正常返回。
- 未充电页、充电中页、充电桩切换、扫码入口、订单结算、统一支付、我的订单和正式订单
  详情页已形成完整 UI 链路；预约与充电状态会同步首页和站点详情的可用桩数量。
- 支付页展示待支付金额、钱包余额与支付后余额；余额不足、提交中、失败、结果未知和重复
  点击限制通过 ViewState/Controller 演示，正式结果必须由服务端确认。

当前 `UserDemoController` 与 `*.tmp` 仍是离线联调适配层。伙伴接入服务端时，应保留页面
信号、ViewState、stationId/chargerId/orderId/operationId 关联和既有跳转语义，逐项替换
tmp 加载、内存余额扣减、内存设备占用、Demo 密码校验与计时模板。UI 不应直接访问数据库、
拼接网络报文或根据按钮点击自行宣告预约、充电、退款及支付成功。

## 22. qmake 工程清单归属（2026-09-09）

`IChargerService`、`IChargingNetworkApi`、`IOrderService`、`IReservationService`、
`IUserNetworkApi` 和 `IWalletNetworkApi` 均带 Qt 元对象声明，但每个头文件只能由所属业务
模块的 `.pri` 登记一次。归属分别为 `charger.pri`、`charging.pri`、`order.pri`、
`reservation.pri`、`user.pri` 和 `wallet.pri`。

`src/network/network.pri` 只登记传输层、BackendClient 和 Real* 网络适配器，不得再次把
上述业务接口加入 `HEADERS`。重复登记会让 qmake 为同一个 `moc_*.cpp` 生成两套 recipe，
出现 `overriding recipe` / `ignoring old recipe`，并导致无效的重复构建。此次已从
`network.pri` 删除六项重复声明；接口源码、MOC 生成和网络依赖均仍由所属模块保留。

伙伴拉取该调整后需清理旧构建目录或至少删除旧 Makefile，再重新运行 qmake；仅增量 make
会继续沿用旧的重复规则。腾讯地图的 WebEngine/WebChannel 探测位于根 `ChargingUser.pro`，
与本次去重无关。

## 23. 协议 v2.6 用户密码链路（2026-09-09）

客户端同时保留两种登录方式，禁止用其中一条替换另一条：手机号免密登录继续使用
`PHONE_LOGIN_REQ(116) → PHONE_LOGIN_ACK(217)`；用户名密码登录使用
`LOGIN_REQ(101) → LOGIN_ACK(201)`，请求字段为 `username/password/role=user`。201 只保证
返回 username/role，客户端登录成功后继续通过既有资料查询刷新 phone、nickname、status
和 balance。302 表示账号或密码错误，仍停留登录页；密码不得写入日志、ViewState 或持久化。

`RealUserNetworkApi` 必须按应答消息号区分 217 与 201。旧实现把 201 先归入手机号登录，
导致 `CredentialLogin` 在途请求无法匹配并最终超时；现已修正为 217→Phone Login、
201→Credential Login。`AppFlowCoordinator::loginByCredentials()` 与手机号登录共享后续
会话刷新和页面分流，但用户名密码失败不缓存密码做自动重试，用户应在保留输入的登录页
重新提交。

密码修改使用 `PROFILE_UPD_REQ(118) → PROFILE_UPD_ACK(228)`：普通修改发送
`username/oldPassword/newPassword/requestId/operationId`；手机号免密账户首次设置密码不发送
oldPassword。服务端 306 + `AUTH_FAIL` 表示旧密码错误，303 + `DUPLICATE` 等业务错误按
原错误映射展示。成功应答必须满足 `ok=true` 且 username 与当前会话一致；`changed` 可用于
诊断，但页面不依赖它宣告成功。

密码修改作为独立 `UserOperation::ChangePassword`，与昵称修改共享 118/228 协议但不共享
业务状态。网络适配层同时只允许一个资料变更请求，优先按 requestId 匹配 228；服务端未
回显 requestId 时，只回退到唯一在途的昵称或密码变更。断线、超时或损坏的 228 均视为
结果未知，锁定重复提交并提示用户退出后分别使用新旧密码登录确认。成功修改后清除临时
旧密码并退出当前会话，要求用新密码重新登录。

UI 的“原密码”步骤仅临时收集输入；v2.6 没有独立的只验证密码接口，因此客户端不能在
进入新密码页面前声称服务端已验证成功。最终提交新密码时一次性发送 118，由服务端原子
校验旧密码并修改。手机号首次资料完善则先设置初始密码，228 成功后再走原昵称保存，避免
两个 118 请求并发。任何密码字符串都不得保存在 PendingRequest、订单数据或 tmp 文件中。
# 头像选择、上传与页面同步

新用户资料完善页和“我的 → 编辑资料”共用 `ProfileEditWindow` 的“更换头像”入口。入口沿用扫码页已经验证的 `QFileDialog::getOpenFileName` 相册读取方式，支持 PNG、JPEG、BMP 和 WebP；客户端先居中裁成正方形，再缩放并转成 JPEG data URI。`AvatarImageHelper` 会逐级降低尺寸和质量，保证传给服务端的整个 `data:image/jpeg;base64,...` 字符串不超过协议规定的 96 KB。取消选图不会发请求，读取失败或无法压缩到限制内会在当前资料页提示。

页面只把最终 data URI 交给 `IUserUiBinder::avatarUpdateRequested`。调用链为 `UserUiBinder → IUserService::updateAvatar → IUserNetworkApi::updateAvatar → RealUserNetworkApi`，正式网络发送 `PROFILE_UPD_REQ(118)`：

```json
{
  "username": "当前登录用户名",
  "avatar": "data:image/jpeg;base64,...",
  "requestId": "客户端请求标识",
  "operationId": "客户端幂等操作标识"
}
```

服务端以 `PROFILE_UPD_ACK(228)` 返回 `ok=true`、同一 `username` 和 `changed:["avatar"]`。成功后 `UserService` 只更新当前会话的 `profile.avatarKey`，不会覆盖手机号、昵称、余额或账号状态；会话变化会同时刷新资料编辑页和“我的”页头像。登录 `101/201`、手机号登录 `116/217` 以及用户查询 `100/200` 返回的 `avatar` 字段也会写入同一属性，因此重新登录和服务端资料刷新后仍以服务端头像为准。

模拟网络实现走相同的 service/binder 接口并回显本次 data URI，便于 `CONFIG+=user_demo` 在不改页面代码的情况下测试。服务端接入时保留 `IUserService` 和 `IUserNetworkApi` 的语义接口，只替换网络适配器即可。
# Demo/main 统一迁移：站点与电桩状态（第一步）

预约与充电服务确认状态变化后，正式入口和 Demo 入口都必须调用 `IMapUiBinder::chargerStatusConfirmed(stationId, chargerId, status)`。公共 `MapUiBinder` 负责立即同步首页可用数、详情页状态、选桩和操作权限，并向当前 `IChargerService` 发起查询校准。禁止页面或入口再次单独扣减 `availableCount`。

数据源差异只保留在 `IChargerService` 实现中：`MockChargerService::applyConfirmedChargerStatus` 更新由 tmp 初始化的内存目录，使后续 Demo 查询返回新状态；`RealChargerService` 不本地伪造服务端数据，因为预约和充电操作已经由服务端持久化，随后通过 119/229 查询校准。两种模式共用状态事件规则：预约成功为 `Reserved`，扫码启动成功为 `Charging`，取消、过期或结束充电为 `Idle`。

迁移前，Demo 使用 `m_demoAvailabilityConsumedKeys` 在渲染首页时重复扣减可用数，而正式 main 只设置预约浮窗，导致 main 的首页和详情可能继续显示旧状态。该集合及渲染期扣减已移除；状态变化现在由公共 Binder 幂等处理，`Reserved → Charging` 不重复扣减，`Charging/Reserved → Idle` 只恢复一次。
