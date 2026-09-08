# ChargingUser UI 网络功能接入协议

更新日期：2026-09-08  
适用分支：`map-navigation-ui`

本文给 UI 开发同学使用。本文只约定 UI 与业务层之间的状态、信号和页面接缝，
不规定页面视觉、布局、颜色或交互稿。

## 1. 当前已完成的功能

正式网络入口为 `src/main.cpp`，业务数据链路为：

```text
页面 → UI Binder → Domain Service → Real Network Adapter → BackendClient → 服务端数据库
```

已经接入的功能：

| 功能 | 网络协议 | 当前状态 |
|---|---|---|
| 登录/用户资料 | 116/217、100/200 | 已接入 |
| 站点和电桩查询 | 119/229 | 已接入 |
| 钱包余额和流水 | 100/200 | 已接入 |
| 钱包充值 | 113/216 | 已接入，结果未知时禁止自动重试 |
| 活动订单恢复 | 106/214 | 已接入 |
| 充电确认 | 119/229 + 100/200 user/orderInfo | 已接入 |
| 停止充电 | 109/209 | 网络适配器已接入 |
| 订单支付 | 115/215 | 网络适配器和结算 Binder 已接入 |
| 预约 | 125/232 | 网络适配器和 Binder 已接入 |
| 服务端推送 | 217/216/215/223/225/226/233 | 已接入分发 |

涉及资金和订单变更的操作仍受能力位保护。服务端尚未确认幂等与结果查询能力前，
启动充电、支付等高风险按钮不能默认启用。

## 2. UI 需要使用的状态合同

UI 不应拼接 JSON、不应直接使用协议消息码、不应直接访问 `BackendClient`。

### 2.1 充电确认页

状态：`ChargeConfirmationViewState`，定义在：

`src/presentation/contracts/chargingviewstates.h`

Binder：`ChargingUiBinder`。

页面需要：

- 监听 `confirmationStateChanged`，调用页面 `render(state)`；
- 发出 `confirmationRefreshRequested()`；
- 发出 `startChargingRequested(stationId, chargerId)`；
- 发出 `rechargeRequested()`；
- `status == ResultUnknown` 时禁止重复启动；
- `canStart == false` 时展示 `disabledReason`。

### 2.2 充电会话页

状态：`ChargingSessionViewState`，定义在：

`src/presentation/contracts/chargingsessionviewstate.h`

Binder：`ChargingSessionUiBinder`。

当前 UI 尚未提供正式会话页面，需要补充页面接缝：

- 监听 `sessionStateChanged`；
- 调用 `refreshRequested()` 刷新订单；
- 调用 `stopChargingRequested()` 停止充电；
- `ResultUnknown` 状态调用 `recoverStopResultRequested()`；
- `Charging` 状态显示 `energyText`、`amountText`、`startedAtText`；
- `Stopping` 和 `ResultUnknown` 状态禁止重复停止。

### 2.3 钱包充值页

状态：`WalletViewState`，定义在：

`src/presentation/contracts/walletviewstate.h`

Binder：`WalletUiBinder`。

已有页面：`WalletRechargeWindow`。

页面需要：

- 监听 `WalletUiBinder::stateChanged`；
- 发出 `rechargeRequested(amountText)`；
- `Loading`/`Submitting` 时禁止重复提交；
- `ResultUnknown` 时禁止自动重试，等待重新进入页面触发权威刷新；
- `recentTransactions` 为服务端返回的最近流水，UI 可自行决定列表、卡片或文本展示。

### 2.4 结算页

状态：`SettlementViewState`，定义在：

`src/presentation/contracts/settlementviewstate.h`

Binder：`SettlementUiBinder`。

业务层已经完成，但正式结算页面尚未补齐。页面需要：

- 监听 `stateChanged`；
- 发出 `payRequested()`；
- `Ready` 显示订单号、金额、支付截止时间；
- `Submitting` 禁止重复支付；
- `Settled` 显示成功态；
- `ResultUnknown` 禁止再次支付，只允许刷新订单/钱包确认。

### 2.5 预约确认页

状态：`ReservationConfirmationViewState`，定义在：

`src/presentation/contracts/reservationviewstates.h`

Binder：`ReservationUiBinder`。

已有页面：`ReservationConfirmationWindow`。

页面需要：

- 监听 `ReservationUiBinder::stateChanged`；
- 发出 `reserveRequested(stationId, chargerId, durationSeconds)`；
- `Submitting` 时禁止重复预约；
- `ResultUnknown` 时禁止自动重发；
- 预约结果由服务端 `232` 返回，不能用本地 Demo 数据替代。

## 3. UI 当前需要补充的内容

以下事项属于 UI 页面接入，不需要修改协议或网络适配器：

1. 新增充电会话页面，并连接 `ChargingSessionUiBinder`。
2. 新增结算页面，并连接 `SettlementUiBinder`。
3. 在真实入口中把停止成功后的订单导航到结算页面。
4. 在登录恢复到 `PendingSettlement` 订单时导航到结算页面。
5. 扫码页面只负责输出 `chargerCode`；不要在 UI 侧判断电桩是否存在，交给服务端确认。
6. 预约页面继续使用现有 `ReservationConfirmationWindow`，只需接收 Binder 状态。
7. 钱包流水展示可自行设计，但数据源必须使用 `WalletViewState::recentTransactions`。

## 4. 服务端推送接缝

推送分发器：`src/network/serverpushdispatcher.h`。

当前已由真实入口消费的推送：

- `balanceChanged()`：刷新钱包和用户资料；
- `paymentNotice(orderId)`：刷新订单详情并进入结算状态；
- `chargingProgress(notice)`：刷新对应订单；
- `chargingFault(notice)`：刷新订单状态；
- `reservationExpired(reservationId, chargerCode)`：预约过期事件，页面可显示提示并刷新站点/钱包。

UI 不需要直接监听 `BackendClient::frameReceived`。

## 5. 结果未知纪律

以下操作一旦超时、断连或收到无法确认的响应，状态必须保持未知：

- 充值；
- 支付；
- 启动充电；
- 停止充电；
- 创建预约。

统一规则：

```text
ResultUnknown → 禁止重复提交 → 查询原操作或权威订单/流水 → 再决定最终展示
```

UI 不得通过再次点击来“重试一次”，也不得本地假设成功或失败。

## 6. 本机数据库联调方式

当前 `protocol.h` 已将服务端地址改为：

```text
SERVER_IP   = 127.0.0.1
SERVER_PORT = 12345
```

联调顺序：

1. 本机启动服务端和数据库；
2. 确认服务端监听 `127.0.0.1:12345`；
3. 准备测试用户、空闲电桩、活动订单和待支付订单；
4. 使用 `CONFIG+=real_network` 构建客户端；
5. 依次验证登录、站点查询、钱包、预约、充电确认、活动订单恢复、停止和支付；
6. 同时核对服务端日志和数据库记录，尤其检查资金流水是否重复。

UI 联调前需要服务端准备：

- 至少一个正常用户；
- 至少一个在线空闲电桩；
- 一个正在充电订单；
- 一个待支付订单；
- 一个可用于预约的空闲电桩；
- 能触发 `225/226/223/233` 推送的测试手段。

## 7. 不要做的事情

- 不要在 UI 中读取 Demo TMP 文件；
- 不要在 UI 中拼接协议 JSON；
- 不要在 UI 中直接使用 `113`、`115`、`125` 等协议常量；
- 不要在未知结果时自动再次提交；
- 不要把本地模拟余额、订单、进度覆盖真实状态；
- 不要把数据库连接放进客户端。

## 8. 充值与钱包返回问题修复记录

### 8.1 充值提示“参数缺失或金额非法（username/amount>0）”

登录、资料和电站查询正常，但充值被服务端拒绝时，优先检查充值请求的金额字段。
协议允许 `amount`（元）或 `amountCents`（分），而联调服务端实现可能仍按 `amount`
字段校验。客户端现同时发送两个字段：`amountCents` 为权威整数分，`amount` 用于
兼容当前服务端实现。

验证步骤：

1. 登录用户并进入钱包页面。
2. 选择 `¥50`，确认服务端收到 `amount=50`、`amountCents=5000`。
3. 收到 `216 RECHARGE_ACK` 后，页面余额应刷新为服务端返回值。
4. 核对 `walletTransaction` 新增一条 `RECHARGE` 流水且不重复。

充值超时或断线仍保持 `ResultUnknown`，不得自动重复提交。

### 8.2 钱包页面返回路径错误

钱包页面需要记录进入来源：从个人中心进入则返回个人中心；从充电确认页进入则
返回充电确认页。返回个人中心时必须恢复底部导航栏，不能固定跳转到充电确认页。

验证步骤：

1. 个人中心 → 钱包 → 返回，应回到个人中心。
2. 充电确认页 → 钱包 → 返回，应回到充电确认页。

## 9. 联调问题清单与修复计划（map-navigation-ui）

本节记录 2026-09-08 联调反馈，作为后续修复的验收依据。当前分支基线为
`4865003`，以下问题尚未宣称已修复。

### 9.1 我的页面四个入口不可用

现象：个人中心的“我的订单、常用充电站、帮助与反馈、关于智充”点击后没有页面变化。

代码证据：`MainWindow` 只对 `profileMenuList` 的第 0 项发出
`ordersPageRequested()`，而 `src/main.cpp` 没有连接该信号，也没有创建或注册
`OrderListWindow`；第 1～3 项没有任何业务分发。

修复顺序：

1. 先接通订单页：创建并注册 `OrderListWindow`，增加 `OrderListUiBinder`（或等价
   的订单查询适配层），使用真实 `IOrderService::queryOrders` 数据，补齐返回路径。
2. 将常用充电站、帮助与反馈、关于智充分别定义明确的页面/占位状态和信号，避免
   点击后静默；暂未接入后端的页面显示诚实的“功能未接入”状态。
3. 为四个入口增加 Qt 信号测试和手工验收：点击、返回、重复进入、登出后重新登录。

### 9.2 首页和详情页腾讯地图不显示

现象：两个地图区域均无法正常显示。

代码证据：`main.cpp` 只从 `TENCENT_MAP_KEY` 或
`config/tencent-map.local.json` 读取 Key；仓库仅提供
`config/tencent-map.example.json`，其中 `key` 为空且 `provider` 为 `mock`。
地图 HTML 又直接加载腾讯 GL JS，Key 为空、域名白名单不匹配、QtWebEngine/WebGL
不可用时都会进入失败态。

修复顺序：

1. 联调前准备不提交到 Git 的 `config/tencent-map.local.json`，填入有效腾讯地图
   JS Key、正确 `region` 和默认坐标；确认 Key 已开通 Web 服务/JavaScript API、
   域名或来源限制允许当前环境。
2. 启动时打印脱敏后的配置状态（是否有 Key、配置来源、WebEngine 进程路径），
   页面失败时保留错误原因和重试按钮。
3. 检查 Linux QtWebEngineProcess、GPU/WebGL 和 HTTPS 访问；必要时提供明确的
   软件渲染启动选项，不把 TMP 地图数据当作真实地图成功标志。
4. 首页与详情页分别验收 `mapReady`、标记渲染、标记点击和地图失败重试。

### 9.3 扫码界面打不开或无实际扫描能力

现象：扫码入口无法进入可用的扫码流程。

代码证据：`main.cpp` 仅把会话页的 `scanChargingRequested` 导航到
`QrCodeScannerWindow`，没有连接 `cameraPermissionRequested`、`scanRetryRequested`、
`imageImportRequested`、`torchToggleRequested`，也没有扫码 Binder/摄像头实现；
页面因此只能显示静态占位状态。

修复顺序：

1. 增加 `QrCodeScannerUiBinder` 与摄像头/图片输入适配器，统一输出扫码状态和解析
   后的 `chargerCode`。
2. 连接权限、重试、相册导入、手电筒信号；解析成功后调用服务端电桩校验，禁止
   UI 本地伪造电桩存在。
3. 校验成功后复用充电确认/启动链路，失败、权限拒绝、无摄像头和取消均有可见状态。

### 9.4 充电桩详情页桩列表不能交互

现象：详情页下方电桩行/“选择”按钮点击无效，导致充电订单无法进入测试。

代码证据：`StationDetailWindow` 已发出 `chargerSelected`，但 `main.cpp` 当前只连接
详情刷新、路线和充电确认信号，没有把 `StationDetailWindow::chargerSelected`
连接到 `IMapUiBinder::chargerSelected`。同时 `MapUiBinder` 只允许
`canCharge` 或本人预约的电桩继续选择；服务端若未返回可启动状态，按钮会按设计禁用。

修复顺序：

1. 先补齐详情页 `chargerSelected → MapUiBinder::chargerSelected` 接线，并在状态
   更新后验证选中态和“去充电/确认”按钮可用性。
2. 对服务端返回的 `online`、业务状态、预约归属和 `canStartCharging` 做字段审计，
   明确不可用原因；不得为了联调直接放开禁用条件。
3. 完成“选择电桩 → 充电确认 → 真实启动 115 → 会话页”的链路测试，并核对服务端
   订单记录。

### 9.5 阶段安排与验收门槛

建议按以下阶段实施，每阶段独立提交：

- A：修复个人中心四入口导航与订单页查询；
- B：补齐详情页电桩选择接线和状态诊断；
- C：恢复腾讯地图配置、WebEngine/WebGL 诊断和双页面验收；
- D：实现扫码输入、服务端电桩校验并接入真实充电确认；
- E：执行端到端联调，覆盖登录、站点、地图、选桩、扫码、启动、活动订单、停止、
  结算和返回路径。

每阶段必须同时满足：代码构建通过、对应自动化测试通过、页面点击有可见结果、
服务端日志与数据库记录一致；未接入的能力只能显示明确占位或错误态，不能静默无响应。

### 9.6 阶段 A 实现状态

阶段 A 已完成首轮代码接线：

- 个人中心四个菜单项均有响应；“我的订单”打开订单页并请求真实活动订单，其他三项
  当前显示明确的功能占位提示；
- `OrderListWindow` 已注册到主窗口页面栈，收到 `activeOrdersReady` 后渲染订单摘要；
- `StationDetailWindow::chargerSelected` 已连接到 `MapUiBinder::chargerSelected`，
  允许选桩状态进入充电确认链路；
- Linux Qt 全量构建 `qmake + make -j4` 已通过。

阶段 A 仍需在服务端联调环境验证真实订单返回字段，以及“选择电桩 → 充电确认”页面
是否按预期切换；阶段 B～D 的地图诊断和扫码实现尚未完成。

### 9.7 阶段 B 首轮实现状态

已增加地图启动诊断：启动日志会输出 Key 是否存在、区域和配置来源；设置环境变量
`CHARGING_TENCENT_DISABLE_GPU=1` 时可在 Linux QtWebEngine 环境启用软件渲染，
用于排查虚拟机 GPU/WebGL 导致的空白地图。该开关只改变渲染诊断行为，不绕过腾讯
地图 Key 校验，也不会替代有效的 `config/tencent-map.local.json` 配置。

### 9.8 阶段 C 首轮实现状态

扫码入口已接通页面状态和所有控件信号：权限申请、重试、图片选择和手电筒操作都会
给出可见结果，选择图片后会明确提示当前尚未安装二维码解析器，不会伪造电桩编号或
创建订单。真正的摄像头采集、二维码解析和服务端电桩校验仍需在下一轮接入专用适配器。
