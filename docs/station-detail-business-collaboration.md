# 充电站详情页业务逻辑与 UI 协作协议

状态：草案，待逻辑负责人、UI 负责人和服务端负责人共同冻结

日期：2026-09-07

适用范围：`ChargingUser` 的“首页站点 → 站点详情 → 选择充电桩 → 充电确认”链路

## 1. 结论与范围

当前仓库已经具备站点详情的只读领域模型、动态电桩列表、页面状态渲染、路线入口、真实网络查询适配器和相应测试，但尚不能把详情页视为可直接接入完整真实业务的成品：

- 默认程序入口只显示登录页，真实站点服务、`MapUiBinder`、详情窗口和页面流转尚未形成正式装配闭环；现有完整页面连接主要位于 Demo 装配。
- 详情页能动态展示任意数量的充电桩，但不能选择某个 `chargerId`；主按钮被固定禁用，无法进入充电确认。
- `IChargerService` 目前只有站点/电桩只读查询，没有启动充电、停止充电、订单查询、支付或结果恢复接口。
- v2 协议已有 108/208、109/209、106/214、115/215、225、226 等消息码和基础字段，但没有可靠回显 `requestId`、`operationId`，也没有按 `operationId` 查询变更结果的消息；因此不能安全实现幂等启动、超时后的结果恢复和并发应答关联。
- 当前详情查询通过 100/200 依次拉取整个 `station` 表和 `charger` 表，再由客户端过滤。该方案可用于小规模联调，但不适合直接定义为生产级真实业务接口。

本阶段必须完成详情页的所有只读按钮响应、动态列表与选择响应，并为订单、钱包、启动/停止充电留下稳定接口。订单、钱包、支付和真实启动本阶段不实现业务内部逻辑。

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

本阶段可以先定义接口与 DTO，不实现真实网络适配。建议订单与充电控制独立于只读 `IChargerService`：

```cpp
struct ChargeConfirmationSnapshot {
    QString stationId;
    QString chargerId;
    QString stationName;
    QString chargerCode;
    QString chargerType;
    std::optional<double> powerKw;
    qint64 priceCentsPerKwh = 0;
    std::optional<qint64> walletBalanceCents;
    bool canStart = false;
    QString disabledReason;
};

struct StartChargingResult {
    QString requestId;
    QString operationId;
    QString orderId;
    QString stationId;
    QString chargerId;
    qint64 priceCentsPerKwhSnapshot = 0;
    QDateTime startedAtUtc;
};

class IChargingService : public QObject {
    Q_OBJECT
public slots:
    virtual void loadConfirmation(const RequestContext &context,
                                  const QString &stationId,
                                  const QString &chargerId) = 0;
    virtual void startCharging(const RequestContext &context,
                               const QString &stationId,
                               const QString &chargerId) = 0;
    virtual void queryOperationResult(const RequestContext &context,
                                      const QString &operationId) = 0;
    virtual void cancelRead(const QString &requestId) = 0;
signals:
    void confirmationReady(const RequestContext &, const ChargeConfirmationSnapshot &);
    void chargingStarted(const RequestContext &, const StartChargingResult &);
    void operationStatusReady(const RequestContext &, const OperationResult &);
    void requestFailed(const ClientError &);
};
```

约束：

- `loadConfirmation` 是只读请求，`operationId` 必须为空。
- `startCharging` 是变更请求，`operationId` 必须非空且由客户端生成；服务端必须按它幂等。
- `queryOperationResult` 查询原操作，不得产生新的启动操作。
- 用户身份从已认证会话获取，不接受 UI 自报 `username` 作为授权依据。
- 钱包余额在确认页仅作展示；本阶段允许为 `null` 并显示“余额待接入”。余额不足是否阻止启动必须由产品/服务端明确，不能由 UI 猜测。
- 后续订单模块应提供 `queryActiveOrder`、`queryOrderDetail`、`stopCharging`、`payOrder`；钱包模块提供 `queryWallet`、`recharge`。所有金额均使用整数分。

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

## 11. 建议实施顺序

1. 共同冻结 ViewState、signals、稳定 ID 和服务端协议缺口。
2. UI 完成动态选择与所有页面状态；逻辑同步完成 Binder 选择规则和 Mock。
3. 正式装配现有真实只读详情与路线能力，完成真实站点详情联调。
4. 新增确认页接口和占位实现，保证详情页可走到确认页但不真实启动。
5. 服务端补齐幂等与关联字段后，再实现启动、订单、停止、结算和钱包。
