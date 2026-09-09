# 充电站详情页业务逻辑与 UI 协作协议

状态：逻辑阶段 1—7 已完成；UI 接入、正式装配和服务端协议仍待完成

首次编写：2026-09-07

最近核对：2026-09-08

适用范围：`ChargingUser` 的“首页站点 → 站点详情 → 选择充电桩 → 充电确认”链路

## 1. 结论与范围

当前仓库已经完成站点/电桩选择、充电确认、启动结果恢复、订单、充电会话、钱包和支付的非 UI 业务合同及状态编排，但尚不能把链路视为可交付成品：

- `MapUiBinder` 已支持按稳定 `chargerId` 选择、刷新后保留/清除选择，并输出是否允许进入确认页；现有 `StationDetailWindow` 尚未消费这些新增字段和意图。
- `ChargingUiBinder`、`ChargingSessionUiBinder` 已实现确认、启动/停止提交防重和结果未知恢复；确认页、充电进行页、结算页尚未由 UI 负责人实现。
- `IOrderService`、`IWalletService`、相关 DTO、Mock 和协议能力闸门已存在；订单结算 Binder、钱包 Binder、历史订单/结算明细合同仍未完成。
- 正式应用装配目前仍以用户模块为主，地图、充电、订单、钱包 Binder 与页面流转尚未形成统一对象图。
- v2.5 虽新增 `119/229` 并保留 `108/208`、`109/209`、`113/216`、`115/215`，仍缺少稳定站点/电桩 ID、可靠回显 `requestId`/`operationId`、幂等规则和按原操作查询结果的合同；真实变更能力因此被显式关闭。

现阶段目标是由 UI 负责人依据已冻结的 ViewState/意图完成页面，由逻辑负责人补齐非网络业务编排和正式装配；协议条件不满足前，不启用真实启动、停止、充值或支付。

## 2. 现有资料及权威关系

实现时按以下优先级处理冲突：

1. 冻结后的公共 C++ 合同与本协议；
2. `docs/map-navigation-collaboration.md`；
3. `docs/station-detail-navigation-collaboration.md`；
4. `docs/charging-flow-collaboration.md`；
5. `协议与接口说明.md` 与 `protocol.h`；
6. 旧 UI、Demo 和固定文案仅作为视觉参考，不作为业务合同。

现有资料可复用，但 `docs/charging-flow-collaboration.md` 中描述的确认页、充电中页、结算页和相关 Service 目前主要是规划，不应误报为已实现。

## 3. 核心原则

### 3.1 动态电桩数量

不同站点的电桩数量不相同，电桩列表必须完全由 `StationDetailViewState.chargers` 驱动：

- 支持 0、1、3、20+ 项，不设固定上限，不创建 `charger1/charger2/charger3` 等槽位，不依赖列表下标表达业务身份。
- 每项必须以稳定的 `chargerId` 为 key。刷新时按 `chargerId` 保留选择、更新状态、插入新增项、移除消失项。
- 桩消失、离线、变为非空闲、故障或 `canCharge=false` 时，立即取消选择并禁用继续按钮。
- 0 个桩是合法数据状态，不等同于网络错误；页面显示“本站暂无充电桩”，保留返回和路线能力。
- 大列表放在滚动区域内；底部操作栏固定，不得被 20+ 项列表遮挡。建议后续从“每次全量重建 QWidget”升级为 `QListView + QAbstractListModel`，以稳定处理频繁刷新和较大列表。

### 3.2 分层边界

- UI 只发出语义化意图并渲染 ViewState，不访问 Service、socket、JSON、协议码或订单表。
- Binder 负责生成 `requestId`，把页面意图连接到业务接口，过滤迟到应答，并将领域对象转换为 ViewState。
- 领域 Service 负责权限、状态机、幂等、价格快照、订单与设备状态一致性；不得依赖 QWidget。
- 服务端是站点、电桩在线状态、业务状态、订单状态、费率和余额的权威来源。
- `stationId`、`chargerId` 是业务标识；名称、编号展示文案和列表下标不能代替它们。

## 4. 详情页 ViewState 合同

现有 `StationDetailViewState` 保留，并新增选择态和刷新时间。建议冻结为：

```cpp
struct StationDetailViewState
{
    QString stationId;
    std::optional<GeoPoint> point;
    MapLoadStatus status = MapLoadStatus::Idle;
    QString name;
    QString address;
    QString availabilityText;
    QString priceText;
    QVector<ChargerListItemView> chargers;
    QString selectedChargerId;
    QString message;
    QString lastUpdatedText;
    bool isRefreshing = false;
    bool canRetry = false;
    bool canNavigate = false;
    bool canContinueToConfirmation = false;
    QString navigationDisabledReason;
    QString chargingDisabledReason;
};
```

不继续复用单一 `disabledReason` 同时解释导航和充电禁用，因为坐标缺失与无可用电桩是两个独立原因。

页面只发出以下意图：

```cpp
void backRequested();
void stationRefreshRequested();
void routePreviewRequested(TravelMode mode);
void chargerSelected(const QString &chargerId);
void chargeConfirmationRequested(const QString &stationId,
                                 const QString &chargerId);
```

UI 不发出无参数 `chargeRequested()`，也不生成 `requestId` 或 `operationId`。

## 5. UI 设计与按钮响应

### 5.1 页面布局建议

从上到下分为：标题栏、站点摘要、地图/路线卡片、价格卡片、电桩列表、固定底部操作栏。

- 站点摘要：名称、地址、可用数/总数、数据更新时间。
- 地图卡片：显示站点位置；坐标缺失时显示明确占位，不把地图错误覆盖到电桩列表。
- 电桩行：编号/名称、类型、功率、在线状态、业务状态、选中态。整行可点击，禁用行不可选但仍可查看禁用原因。
- 底部操作栏：次按钮“路线”，主按钮“选择电桩”或“继续”。主按钮不应替代列表选择；它只在合法选中后进入确认页。

### 5.2 各按钮响应

| 控件/动作 | 可用条件 | 点击响应 | 失败/禁用反馈 |
|---|---|---|---|
| 返回 | 始终可用 | 返回来源页并保留首页查询、滚动和选中站点 | 不发网络请求 |
| 重新加载 | 当前非 Loading | 以当前 `stationId` 发起新请求；保留上次成功内容并显示局部刷新态 | 可重试错误显示原因；不得清空后闪白 |
| 路线 | Ready 且坐标有效 | 默认进入驾车路线预览，预览页可切换步行 | 坐标缺失、地图不可用时显示独立原因 |
| 电桩行 | Ready 且该项 `canCharge=true` | 发出 `chargerSelected(chargerId)`，显示单选态 | 离线/占用/预约/故障/重启/未知分别说明 |
| 继续/充电确认 | Ready、账号状态允许、站点和选中桩有效 | 发出 `chargeConfirmationRequested(stationId, chargerId)`；只加载确认快照，不启动充电 | 未选择时提示“请先选择可用充电桩”；不得静默无响应 |

所有点击在发出意图后都必须有即时视觉反馈。重复点击由 UI 禁用和 Binder 去重共同防护。

### 5.3 页面状态

- `Idle`：展示骨架/占位，不展示伪造站点数据。
- 首次 `Loading`：骨架屏或加载提示，路线和继续禁用。
- 刷新 `Loading`：保留上次成功内容，加“正在刷新”；选择暂时保留，但继续按钮禁用，直到新快照确认。
- `Ready + chargers 非空`：正常列表。
- `Ready + chargers 为空`：合法空态，路线仍可用。
- `Error + 无缓存`：错误说明和按 `canRetry` 决定是否显示重试。
- `Error + 有缓存`：展示旧内容和“刷新失败/数据可能已变化”，禁止以旧状态启动充电。

## 6. 业务流程

### 6.1 打开详情

```text
首页点击查看详情(stationId)
  → Binder 校验 stationId，生成 requestId
  → IChargerService::queryStationDetail
  → 发布 Loading
  → 成功后按 chargerId 构造列表并发布 Ready
  → 失败后发布 Error；迟到 requestId 不得覆盖当前页面
```

### 6.2 选择与刷新

```text
选择可用 chargerId
  → Binder 校验该 ID 存在且 canCharge
  → 更新 selectedChargerId
  → 刷新详情
  → 同 ID 仍可用：保留选择
  → 同 ID 消失或不可用：清除选择并提示状态已变化
```

### 6.3 进入确认页

详情页点击“继续”只打开确认页。逻辑层必须重新获取一次权威确认快照，核对：用户登录且身份状态已知、非 Frozen、站点存在、桩在线且空闲、价格有效、无冲突中的 Charging 订单。确认页展示站点、桩、功率、当前电价及“实际费用以充电结束结算为准”。此步骤不得创建订单。

### 6.4 启动充电（预留）

```text
确认页点击开始充电
  → Binder 禁用提交并生成唯一 operationId
  → IChargingService::startCharging(...)
  → 服务端在同一事务中复核权限/桩状态、创建 Charging 订单并占用电桩
  → 成功返回 orderId 和价格快照
  → 超时/断线：ResultUnknown，使用原 operationId 查询结果，禁止创建新 operationId 盲目重试
```

## 7. 订单、钱包与充电接口预留

接口与 DTO 已实现，订单与充电控制保持独立于只读 `IChargerService`。权威定义以代码为准：

```cpp
// src/modules/charging/ichargingservice.h
loadConfirmation(context, stationId, chargerId);
startCharging(context, stationId, chargerId);
queryStartResult(readContext, originalOperationId);

// src/modules/order/iorderservice.h
queryActiveOrder(readContext);
queryOrderDetail(readContext, orderId);
stopCharging(mutationContext, orderId);
queryStopResult(readContext, originalOperationId);

// src/modules/wallet/iwalletservice.h
queryWallet(readContext);
recharge(mutationContext, amountCents);
payOrder(mutationContext, orderId);
queryOperationResult(readContext, originalOperationId);
```

约束：

- `loadConfirmation` 是只读请求，`operationId` 必须为空。
- `startCharging` 是变更请求，`operationId` 必须非空且由客户端生成；服务端必须按它幂等。
- `queryStartResult`、`queryStopResult`、`queryOperationResult` 都只查询各自原操作，不得产生新的变更操作。
- 用户身份从已认证会话获取，不接受 UI 自报 `username` 作为授权依据。
- 钱包余额在确认页仅作展示；本阶段允许为 `null` 并显示“余额待接入”。余额不足是否阻止启动必须由产品/服务端明确，不能由 UI 猜测。
- 订单模块已提供 `queryActiveOrder`、`queryOrderDetail`、`stopCharging`；支付归属钱包模块的 `payOrder`。所有金额均使用整数分。

## 8. 服务端协议必须补齐的字段

现有 108/208 可以保留消息码，但上线真实变更业务前至少补齐：

```json
// 108 START_CHARGING_REQ
{
  "requestId": "req-uuid",
  "operationId": "op-uuid",
  "stationId": "station-stable-id",
  "chargerId": "charger-stable-id"
}

// 208 START_CHARGING_ACK
{
  "requestId": "req-uuid",
  "operationId": "op-uuid",
  "orderId": "order-stable-id",
  "stationId": "station-stable-id",
  "chargerId": "charger-stable-id",
  "priceCentsPerKwhSnapshot": 150,
  "startedAt": "2026-09-07T12:00:00Z"
}
```

还必须新增或冻结“按 `operationId` 查询结果”接口，并让所有成功/错误应答回显 `requestId`、`operationId` 和原请求类型。错误至少区分：未登录、账号冻结、站点不存在、桩不存在、桩离线、桩已占用、已有进行中订单、价格无效、余额策略限制、幂等冲突、服务端忙。

站点详情建议增加专用查询接口，服务端一次返回站点和所属桩，不继续依赖客户端全表拉取：

```json
{
  "requestId": "req-uuid",
  "stationId": "station-stable-id"
}
```

响应中的 `chargers` 为数组，长度可为 0 到任意合理上限；服务端返回稳定 ID、在线状态和业务状态。客户端仍需设置响应大小、数量和字符串长度上限，防止异常数据造成 UI/内存问题。

## 9. 分工

### 9.1 UI 负责人

- 在 `StationDetailWindow` 实现电桩单选、选中态、不可用态、禁用原因和主按钮即时反馈。
- 按本协议拆分导航与充电的禁用原因；补充刷新中、缓存错误、0 桩、长文本和 20+ 桩状态。
- 保证固定底部操作栏、键盘焦点、滚动、重复点击防护和可访问性。
- 只使用 ViewState 和语义信号；不创建订单、拼 JSON、引用协议码或计算价格。
- 提供 0、1、3、20+ 桩以及状态变更的 UI 演示夹具；夹具不得进入正式业务路径。

### 9.2 逻辑负责人

- 冻结 ViewState、页面意图和 `IChargingService`/DTO；实现选择校验、刷新保留/失效规则和迟到应答隔离。
- 将 `RealChargerService`、`MapUiBinder`、窗口与 Flow 正式装配到真实应用入口，避免只在 Demo 可用。
- 保持详情查询为只读；为确认快照、启动、结果恢复提供独立接口和 Mock。
- 与服务端确认稳定 `stationId`/`chargerId`、专用详情查询、幂等和应答关联；协议未冻结前不得用页面状态猜应答。
- 补齐单元测试、Binder 测试和真实适配器协议测试。

### 9.3 共同维护的接缝

以下变更必须先同步并以小提交完成：`StationDetailViewState`、`ChargerListItemView`、页面 signals、`IChargingService`、`.pro/.pri`、`.ui objectName`、资源别名和页面注册/流转。

## 10. 验收标准

- 0、1、3、20+ 个桩均按数据动态渲染，无固定槽位、越界、重叠或底栏遮挡。
- 点击可用桩后只选中一个；刷新后按 `chargerId` 正确保留或清除。
- 返回、刷新、路线、桩选择、继续五类交互都有明确响应；禁用时均有原因。
- 首次加载、保留内容刷新、空态、可重试错误、不可重试错误和迟到应答均有测试。
- 未选择桩、账号 Frozen/Unknown、桩离线/占用/故障、坐标缺失等边界不进入错误业务流程。
- 详情页及其 Binder 不依赖订单/钱包的具体实现，缺失数据有明确占位。
- 变更请求具备非空 `operationId`；结果未知时只查询原操作，不盲目重试。
- 在 BitDev Ubuntu 22.04、Qt 6.2.4、`qmake6` 环境完成构建、自动测试和 UI 验收；未执行前只能报告“未验证”。

## 11. 初始实施进度

1. ViewState、业务接口、稳定 ID 约束和服务端协议缺口已经形成合同。
2. Binder 选择规则、确认/会话编排及 Mock 已完成；动态选择和页面状态 UI 待 UI 负责人完成。
3. 真实只读详情与路线已有基础实现；正式应用装配和完整联调待完成。
4. 确认、订单、停止、钱包、支付接口已预留；对应页面及钱包/结算 Binder 待完成。
5. 真实启动、停止、充值和支付必须等待服务端补齐幂等、关联和结果查询合同。

## 12. 2026-09-08 实现基线

以下非 UI 能力已完成并有自动测试，不应由页面重复实现：

| 能力 | 权威代码 | 当前结论 |
|---|---|---|
| 动态电桩选择与选择失效 | `MapUiBinder`、`StationDetailViewState` | 已完成；按 `chargerId` 工作，不依赖数量或下标 |
| 充电确认状态编排 | `ChargingUiBinder`、`ChargeConfirmationViewState` | 已完成；含刷新、防重、迟到应答隔离和启动结果恢复 |
| 真实充电能力闸门 | `ChargingService`、`ChargingBackendCapabilities` | 已完成；v2.5 默认禁止不安全变更 |
| 订单边界 | `IOrderService`、`ChargingOrder`、`MockOrderService` | 已完成基础查询/停止/结果查询合同；真实适配未实现 |
| 充电会话编排 | `ChargingSessionUiBinder`、`ChargingSessionViewState` | 已完成基础详情刷新、停止防重和结果恢复 |
| 钱包与支付边界 | `IWalletService`、`WalletService`、`WalletBackendCapabilities` | 已完成；金额为整数分，v2.5 默认禁止不安全资金变更 |

这些能力位于提交 `15cbea5` 至 `8955c72`。页面不得复制其中的校验、金额计算、请求标识或操作恢复逻辑。

## 13. UI 负责人执行清单

UI 负责人拥有 `src/presentation/pages/**`、`ui/**`、样式和资源文件。逻辑负责人不在这些文件中决定布局或视觉方案。双方只通过 ViewState、页面意图和页面注册接缝协作。

### 13.1 站点详情页

修改现有 `StationDetailWindow`：

- `render()` 必须完全按 `StationDetailViewState.chargers` 渲染 0、1、3、20+ 个电桩，不创建固定数量槽位。
- 每个可用电桩行发出 `chargerSelected(chargerId)`；选中态只取自 `selectedChargerId`，页面不得自行保存第二份选择真相。
- 主按钮仅在 `canContinueToConfirmation=true` 时启用，点击发出 `chargeConfirmationRequested(stationId, selectedChargerId)`。
- 路线按钮使用 `navigationDisabledReason`，充电按钮使用 `chargingDisabledReason`，禁止继续使用一个通用原因覆盖两个动作。
- `isRefreshing=true` 时保留旧内容但禁止继续；0 桩显示合法空态；不可用行展示 `disabledReason`。
- 大列表必须可滚动且不遮挡底部按钮。列表实现形式由 UI 负责人决定，业务层不要求特定控件。

### 13.2 充电确认页

新增确认页并只消费 `ChargeConfirmationViewState`：

- 展示站点、地址、电桩、类型、功率、状态、电价和钱包余额。
- 页面发出返回、重新核对、去充值、开始充电四类语义意图。
- `Loading`、`Submitting`、`ResultUnknown` 期间按 ViewState 禁止重复提交；不得用 UI 定时器模拟启动成功。
- `canStart=false` 时展示 `disabledReason`；余额缺失显示“待接入”，不得按展示字符串推断余额策略。
- 开始按钮不得生成 `requestId`、`operationId` 或订单号。

### 13.3 充电进行页

新增充电会话页并只消费 `ChargingSessionViewState`：

- 覆盖 `Loading`、`Charging`、`Stopping`、`ResultUnknown`、`Ended`、`Error`。
- 展示订单号、站点、电桩、开始时间、已充电量和当前金额；缺失字段使用占位，不自行估算费用。
- 页面发出刷新、停止充电、恢复停止结果意图。
- 只有 `canStop=true` 时允许停止；`canRecoverResult=true` 时显示“继续确认结果”，不得重新提交停止操作。

### 13.4 钱包、支付与结算页

- 现有 `WalletRechargeWindow` 只负责金额输入和展示；金额字符串的合法性、元转分和上限校验交给后续 `WalletUiBinder`。
- 结算页只展示后续 `SettlementViewState` 的权威金额明细，不在页面内相加电费、服务费、停车费或优惠。
- 支付、充值提交中和结果未知时禁止返回操作造成“看似取消”；是否允许离开由 Binder 状态决定。
- UI 可完成禁用态和演示夹具，但协议能力关闭时不得通过按钮或本地状态绕过能力闸门。

### 13.5 UI 自测矩阵

UI 提交至少覆盖：

- 电桩数量 0、1、3、20+，以及选中桩刷新后消失或不可用；
- 长站名、长地址、长错误原因、空价格和空余额；
- 首次加载、保留内容刷新、空态、可重试/不可重试错误；
- 快速双击继续、开始、停止、充值和支付时只发出一次有效意图；
- 键盘焦点、滚动、固定底栏、禁用原因和可访问名称；
- 页面返回后仍保持正确的 `stationId`、`chargerId`、`orderId`。

## 14. 除真实网络功能外的未完成项

### P0：形成可运行的正式业务闭环

1. **正式应用装配**：当前 `UserApplicationAssembly` 只装配用户模块。需要建立应用级对象图，注入地图、充电、订单、钱包 Service，并暴露对应 Binder；对象生命周期由装配层统一管理。
2. **页面流转协调**：扩展 M4，使链路成为“首页 → 详情 → 确认 → 充电中 → 结算/订单详情”，以及“确认 → 充值 → 返回后重新核对”。页面不得互相直接持有并决定业务跳转。
3. **UI 接入**：完成第 13 节四组页面及信号连接。业务 Binder 已存在的功能不得在 Widget 中重新实现。
4. **移除兼容接缝**：新 UI 接通后，评审并删除无参数 `chargeRequested()`、旧 `navigationRequested()` 等兼容信号，避免新旧链路同时触发。删除必须独立提交并先确认没有其他调用方。

### P1：补齐非网络业务编排

1. **钱包 Binder**：新增 `WalletViewState`/`WalletUiBinder`，负责严格解析充值金额、元转整数分、上下限校验、提交防重、迟到应答隔离和结果未知恢复。
2. **结算与订单 Binder**：新增 `SettlementViewState`、`OrderDetailViewState` 和相应 Binder，处理待支付、支付中、结果未知、余额不足、已结算、已取消等状态。
3. **结算明细合同**：订单 DTO 目前只有总金额。需与产品/服务端冻结电费、服务费、停车费、优惠、应付、实付、支付截止时间和退款字段；UI 不自行拆分或计算。
4. **活动订单恢复**：登录成功和应用恢复前台时调用 `queryActiveOrder`，存在 Charging 订单则恢复充电会话，存在 PendingSettlement 订单则进入结算提醒；不得依赖上次打开的页面判断。
5. **充电过程刷新策略**：定义 `IChargingProgressSource` 或等价抽象，统一处理推送、轮询、退避、前后台切换和数据新鲜度。即使暂用 Mock，也不能在页面内部累计电量或金额。
6. **统一操作恢复**：把启动、停止、充值、支付共同需要的 operationId 保存与恢复策略抽出，至少保证进程内不重复提交；是否需要跨进程持久化必须在产品确认后决定。
7. **业务权限汇总**：明确 Frozen、Unknown、余额不足、已有活动订单分别对启动、充值、支付、停止的影响，由 Service/Binder 输出最终 `canXxx`，UI 不组合推断。

### P2：质量、产品决策与可维护性

1. **错误模型统一**：冻结错误码到用户文案、是否可重试、是否结果未知和推荐动作的映射，避免各 Binder 自带不一致中文文案。
2. **边界和容量限制**：冻结最大电桩数、最大订单/流水页大小、字符串长度、金额范围和时间格式；超限数据必须可诊断且不拖垮页面。
3. **时间与金额规则**：统一 UTC 输入、本地展示、舍入规则和货币单位；禁止 `double` 参与资金计算。
4. **分页与历史记录**：若产品要求订单历史和钱包流水，增加游标分页合同、空态、刷新和去重规则；目前只定义了钱包快照中的近期流水。
5. **可观测性**：为 requestId、operationId、订单状态迁移和能力闸门拒绝增加脱敏日志；日志不得记录 token、完整手机号或支付敏感信息。
6. **测试补齐**：增加应用装配测试、全链路状态测试、重启恢复测试、错误映射测试和 UI 合并后的回归测试；最终在 BitDev Qt 6.2.4 做完整工程构建与测试。

## 15. 剩余工作实施顺序

1. **合同冻结**：逻辑与 UI 共同确认页面意图、四组 ViewState、结算字段、充值金额规则和页面返回策略；先提交纯合同变更。
2. **逻辑补齐**：实现 Wallet/Settlement/Order Binder、活动订单恢复、进度源抽象和统一错误映射，每个模块配独立测试。
3. **UI 并行开发**：UI 负责人按第 13 节完成页面，只依赖已冻结合同；不得修改 Service 内部规则。
4. **正式装配**：建立统一应用对象图与 M4 流转，先用 Mock 完成全链路，不等待真实网络协议。
5. **联合验收**：合并 UI 后覆盖动态数量、快速重复点击、迟到应答、结果未知、返回恢复和应用重启场景。
6. **真实网络阶段**：服务端协议补齐后再实现适配器、开启 capabilities，并进行联调、故障注入和兼容测试；不得只因消息码存在就开启变更按钮。

每一步都应保持“合同 → 逻辑 → UI → 装配”的依赖方向。涉及共享合同或 `.pri/.pro` 时先同步，禁止用整文件覆盖解决冲突。
