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
