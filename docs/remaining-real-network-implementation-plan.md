# ChargingUser 剩余功能与真实服务端接入规划

更新日期：2026-09-08  
规划基线：`map-navigation-ui` 最新提交 `7b33a4e`，并参考 `feature/socket-thread-transport` 的真实网络集成提交 `35470dc` 与合并提交 `3eb585a`。

## 1. 目标

将用户端现有页面和后续充电业务全部接入服务端，使正式构建中的业务数据只来自服务端数据库或腾讯地图服务。保留 `src/demo/*.tmp` 作为教学演示与自动测试夹具，但正式网络入口不得读取这些文件。

最终业务闭环：

```text
手机号登录
  → 首页加载真实站点与电桩
  → 站点详情与电桩选择
  → 充电确认
  → 启动充电
  → 充电进度
  → 停止充电
  → 订单结算与支付
  → 钱包、订单和电桩状态刷新
```

预约闭环：

```text
站点详情/扫码
  → 预约确认
  → 创建预约并扣除押金
  → 预约状态展示
  → 开始充电或预约过期
  → 押金退款或预约失效
```

## 2. 当前进度

### 2.1 已完成

- `map-navigation-ui` 已提供首页、站点详情、路线导航、充电确认、二维码扫码和预约确认 UI。
- Socket 传输、工作线程、协议拆包/组包、心跳、断线重连和服务端地址参数已实现。
- `RealUserNetworkApi` 已实现手机号登录、用户资料查询和昵称修改。
- 用户资料能够接收 `balanceCents` 并显示真实余额。
- `RealChargerService` 已通过 `GETDATA(100) / DATA(200)` 查询 `station` 与 `charger` 表。
- 站点坐标已兼容数据库正式字段 `longitude/latitude`。
- `TencentMapService`、真实地图 Key 配置、地图 Binder 和真实入口基础装配已实现。
- 正式构建与 Demo 构建已隔离：Demo TMP 文件保留，但正式网络入口不读取。

### 2.2 尚未完成

- 地图 UI 新增的充电确认、扫码、预约页面尚未全部装配到真实入口。
- 订单、钱包、启动充电、停止充电、支付和预约没有完整的真实网络适配器。
- “充电”主页仍缺少活动订单自动恢复和实时进度展示。
- 钱包充值页面存在，但缺少正式 Wallet Binder 和真实充值处理。
- 历史订单、钱包流水和待支付结算页尚未形成完整 UI/业务合同。
- 服务端是否完整回显 `requestId/operationId`、是否支持幂等重试和结果查询仍需逐项验证。

## 3. 架构约束

正式数据链路统一为：

```text
QWidget
  → UI Binder
  → Domain Service
  → Real Network Adapter
  → BackendClient
  → ClientSocketThreadManager
  → 服务端
  → 数据库
```

约束如下：

- 页面只发送语义化信号、渲染 ViewState，不拼接 JSON，不使用协议码。
- 业务 Service 不依赖 QWidget、Socket 或数据库实现。
- 真实适配器负责协议码、JSON、超时、错误映射和响应关联。
- 所有业务模块共享 `BackendClient`，不各自创建 Socket。
- 金额统一使用整数分 `qint64`，只在 UI 展示时转换为元。
- 日志不得打印完整手机号、密码、地图 Key、Token 或完整业务载荷。
- 当前暂不处理多请求并发；同一无关联响应类型一次只允许一个在途请求，并显式返回 busy 错误。
- TMP 仅用于 `CONFIG+=user_demo`，不得作为真实请求失败时的静默回退数据。

## 4. 分阶段实施计划

### 阶段 A：冻结构建模式与启动入口

目标：消除“运行了 Demo 却以为在访问数据库”的问题。

工作项：

- `ChargingUser.pro` 默认启用 `real_network`。
- `user_demo` 必须显式指定，并使用独立输出目录。
- `resources/demo-resources.qrc` 只在 Demo 模式加载四个 TMP。
- 正式二进制不得包含 `map-demo-data.tmp` 等资源名或 Mock 服务符号。
- 程序启动日志明确输出 `real-network` 或 `user-demo` 模式。
- Qt Creator Debug/Release/Profile 配置统一检查 qmake 参数和运行目标。

验收：普通 `qmake && make` 生成真实网络程序；删除 TMP 不影响真实构建，但 Demo 构建要求 TMP 存在。

### 阶段 B：合并后真实入口重新装配

目标：把最新地图 UI 的页面接到真实对象图，而不是只存在于 Demo Controller。

主要文件：

```text
src/main.cpp
src/app/application.*
src/app/mapuibinder.*
src/app/charginguibinder.*
src/app/chargingsessionuibinder.*
src/presentation/pages/charging/*
```

工作项：

- 创建并注册充电确认、扫码、预约确认、钱包充值页面。
- 连接站点详情的充电、预约和扫码信号。
- 统一管理首页、详情、导航、确认、充电中、充值和预约页面流转。
- 登录成功后刷新用户、站点、钱包与活动订单。
- 退出登录时取消在途请求并清除账号相关 ViewState。

验收：真实入口中所有新页面均可到达；页面不读取 Demo Controller 或 TMP。

### 阶段 C：真实站点与电桩查询完善

目标：确保首页、地图标记、站点详情和确认页使用同一份数据库权威数据。

主要文件：

```text
src/network/realchargerservice.*
tests/charger/real-charger-service-tests.*
```

工作项：

- 优先评估 `STATION_QRY_REQ(119) / STATION_QRY_ACK(229)`；必要时继续兼容 `100/200` 两表查询。
- 完整映射 `stationName/address/longitude/latitude/priceCents`。
- 完整映射 `chargerCode/stationName/chargerIndex/type/power/businessStatus/online`。
- 明确 `stationId/chargerId`：当前可暂用 `stationName/chargerCode`，同时标注其稳定性限制。
- 增加无数据、脏行、重复数据、非法坐标、离线和故障状态测试。
- 不允许真实查询失败后自动展示 TMP 站点。

验收：修改服务端站点或电桩后，客户端刷新可观察到一致变化。

### 阶段 D：真实订单只读能力

目标：先完成订单读取和活动订单恢复，再开放变更操作。

新增文件：

```text
src/network/realorderservice.h
src/network/realorderservice.cpp
tests/order/real-order-service-tests.cpp
tests/order/real-order-service-tests.pro
```

协议：

- `ORDERQRY_REQ(106) → ORDERQRY_ACK(214)`
- 必要时使用 `GETDATA(100) → DATA(200)` 查询 `orderInfo`

工作项：

- 查询当前用户的 Charging/PendingSettlement 活动订单。
- 按 `orderNo` 查询订单详情。
- 解析站点、电桩、状态、电量、金额、开始/结束时间和支付截止时间。
- 登录后自动恢复活动订单。
- 暂不实现历史订单分页时，UI 明确显示“历史订单待接入”。

验收：数据库中活动订单变化后，客户端“充电”页刷新一致。

### 阶段 E：真实钱包查询与充值

目标：余额和流水都由服务端权威返回，并打通充值页面。

新增文件：

```text
src/network/realwalletnetworkapi.*
src/app/walletuibinder.*
src/presentation/contracts/walletviewstates.h
tests/wallet/real-wallet-network-tests.*
```

协议：

- `GETDATA(100) → DATA(200)`：查询 `user.balanceCents` 与 `walletTransaction`
- `RECHARGE_REQ(113) → RECHARGE_ACK(216)`

工作项：

- 钱包余额与近期流水查询。
- 充值金额严格解析、范围校验和元转分。
- 充值提交期间禁止重复点击。
- 成功后重新查询用户资料和钱包流水。
- 超时且结果未知时不自动重发充值，先查询权威余额/流水。

验收：充值后数据库余额、流水和客户端展示一致且只增加一笔流水。

### 阶段 F：真实充电确认与启动

目标：从选桩进入权威确认，再创建真实 Charging 订单。

新增文件：

```text
src/network/realchargingnetworkapi.*
tests/charging/real-charging-network-tests.*
```

协议：

- `STATION_QRY_REQ(119) → STATION_QRY_ACK(229)`
- `START_CHARGING_REQ(108) → START_CHARGING_ACK(208)`

工作项：

- 确认页重新查询站点、电桩、价格、余额、账号和活动订单。
- 校验电桩在线、空闲、非故障且属于目标站点。
- 使用已登录 `username` 和真实 `chargerCode` 启动充电。
- 解析 `orderNo/priceCents/startedAt`。
- 明确并测试服务端对重复请求的行为。
- 服务端未支持幂等或操作结果查询前，超时后只允许权威查询，不盲目重发。

验收：启动后数据库新增一笔 Charging 订单，电桩状态变为 Charging，客户端进入对应充电会话。

### 阶段 G：实时充电进度与异常

目标：充电过程不使用本地计时或金额模拟。

协议：

- `CHG_PROGRESS(225)`
- `CHG_FAULT_NOTICE(226)`
- `ORDERQRY_REQ(106) / ORDERQRY_ACK(214)` 作为刷新与恢复通道

工作项：

- 新增推送分发组件，将服务端通知路由到当前用户和订单。
- 展示电量、金额、百分比、剩余时间及更新时间。
- 断线重连后按订单号重新查询，不沿用过期本地状态。
- 异常通知后刷新订单和电桩状态。
- 应用前后台切换时定义轮询与刷新策略。

验收：服务端上报进度后客户端实时变化；重连后能恢复正确订单状态。

### 阶段 H：停止充电与结算

协议：

- `STOP_CHARGING_REQ(109) → STOP_CHARGING_ACK(209)`
- `PAYMENT_NOTICE(223)`

工作项：

- 根据当前订单取得 `chargerCode`，发送停止请求。
- 解析最终电量、金额和支付截止时间。
- 进入 PendingSettlement 页面，并重新查询订单详情确认结果。
- 超时不盲目重复停止；优先查询订单状态。
- 停止成功后刷新站点电桩状态。

验收：订单进入 PendingSettlement，电桩释放，最终金额与数据库一致。

### 阶段 I：订单支付

协议：

- `PAY_REQ(115) → PAY_ACK(215)`

工作项：

- 新增结算 ViewState 与 Binder。
- 展示订单号、电量、金额、余额和截止时间。
- 处理余额不足、订单状态冲突、重复支付和账号冻结规则。
- 支付成功后刷新订单、钱包余额和流水。
- 结果未知时通过订单状态和流水确认，不重复扣款。

验收：支付后订单为 Settled，余额和 PAY 流水一致，不产生重复扣款。

### 阶段 J：预约业务

新增文件：

```text
src/network/realreservationservice.*
src/app/reservationuibinder.*
tests/reservation/real-reservation-service-tests.*
```

协议：

- `RESERVE_REQ(125) → RESERVE_ACK(232)`
- `RESERVE_EXPIRED_NOTICE(233)`
- `GETDATA(100) → DATA(200)`：查询 `reservation`

工作项：

- 查询当前预约并恢复页面状态。
- 创建预约，展示押金、预约时间和过期时间。
- 处理已有预约、目标桩被占用、余额不足和账号冻结。
- 预约成功后刷新钱包、流水和电桩状态。
- 处理过期通知；开始充电后确认押金 REFUND 流水。
- 扫码结果只解析 `chargerCode`，仍由服务端查询并校验电桩。

验收：预约、扣押金、过期或开始充电退款均与数据库一致。

### 阶段 K：历史记录与辅助功能

工作项：

- 我的订单列表与详情。
- 钱包流水列表。
- 分页、空态、加载失败和重试。
- 常用充电站需先确认服务端表结构；未确认前不使用本地固定列表。
- 帮助、反馈和关于页面属于静态内容，不需要数据库。

## 5. 服务端协议核对清单

客户端变更功能开始前必须逐项实测：

- `stationName`、`chargerCode`、`orderNo` 是否唯一且不可变。
- 108、109、113、115、125 的成功和错误响应是否回显 `requestId`。
- 是否回显 `operationId`，或者是否提供等价幂等键。
- 相同操作重复到达时是否返回同一结果，而不是重复写数据库。
- 是否提供按 operationId 查询原操作结果的接口。
- 用户会话如何绑定到 Socket，哪些请求必须携带 `username`。
- 订单、电桩、余额和流水是否在同一事务内更新。
- 225、226、223、233 推送的目标用户路由规则。
- 时间字段格式和时区。
- 金额字段的权威单位及范围。

学习项目若暂时接受弱幂等，应在真实适配器中以 `TEST_ONLY` 注释标明，并禁止自动重试资金和订单变更请求。

## 6. 测试计划

### 6.1 自动测试

- 协议帧拆分、粘包、大消息和坏 JSON。
- 每个真实适配器的请求载荷、应答映射和错误映射。
- 请求超时、断连、取消、迟到响应和 busy 防并发。
- Binder 的 Loading/Ready/Empty/Error/ResultUnknown 状态。
- 页面流转和退出登录后的数据清理。
- 正式构建不包含 TMP/Mock；Demo 构建可正常加载所有 TMP。

### 6.2 隔离数据库联调

- 使用专用测试用户、站点、电桩和订单。
- 先执行只读查询，再执行充值、预约、启动、停止和支付。
- 每一步同时核对客户端、服务端日志和数据库记录。
- 变更测试记录测试账号与新增记录，结束后由服务端负责人处理测试数据。

### 6.3 双机验收

- Linux A：运行服务端和数据库。
- Linux B：运行 ChargingUser 客户端。
- 验证断网、重连、服务端重启、超时和推送恢复。
- 验证另一台相同配置机器无需本地数据库或 TMP 即可完成业务。

## 7. 提交与分支建议

建议继续使用小阶段提交：

```text
build: isolate real and demo targets
feat(order): add real order queries
feat(wallet): connect wallet and recharge
feat(charging): connect confirmation and start
feat(charging): handle progress and stop
feat(payment): add settlement flow
feat(reservation): connect reservation service
test(network): add remote integration coverage
```

每阶段顺序：实现与单元测试 → Linux Qt6 构建 → 隔离数据库联调 → 展示变更摘要 → 提交。推送前单独确认。

## 8. 推荐执行顺序与里程碑

1. A：构建模式和运行目标完全可辨识。
2. B+C：最新地图 UI 使用真实站点/电桩，彻底消除误读 TMP。
3. D+E：订单和钱包只读数据贯通。
4. F：真实启动充电。
5. G+H：进度、异常、停止和结算。
6. I：支付闭环。
7. J：预约闭环。
8. K：历史记录与辅助功能。
9. 全量双机验收、文档更新和最终交付。

第一轮下一步应执行阶段 A、B、C，并用服务端数据库中人为修改的一条站点记录做可观察验证。只有客户端展示该修改且正式二进制中不包含 Demo 资源时，才进入订单和资金业务。
