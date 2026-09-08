# 用户相关功能真实实现规划

日期：2026-09-08；分支：`map-navigation-ui`（基线 `a35d81d`）；负责人：逻辑负责人。

本文规划用户域（登录/会话/资料/余额/钱包/推送）从 Mock 切换到真实服务端的全过程。
目标服务端：另一台主机上运行的充电桩服务端进程（数据库只允许服务端访问）。

## 0. 架构边界（先说清楚）

- **客户端绝不直连数据库。** 与"另一台主机"的连接 = TCP 连到该主机的服务端进程
  （默认 `127.0.0.1:12345`，远程即 `<主机IP>:12345`），数据一律经协议消息提取。
  直连数据库会绕过鉴权/权限矩阵、破坏 ResultUnknown 纪律，且与合同冲突。
- 连接层（`QtNetworkTransport`/`BackendClient`）已具备自动重连与心跳，远程主机
  只需改目标地址；网络抖动语义（超时/断连/迟到响应）沿用 RealChargerService 的模式。
- 协议基线：仓库 `protocol.h` 仍为 v2.4（v2.5 冲突未合入，见
  `docs/protocol-v2.5-conflict-report.md`）。**用户域不受 3.1/3.2 冲突阻塞**
  （用户表以 username 为权威键，116/217/102/100/113/216 均为 v2.4 已验证消息）。

## 1. 现状盘点

| 能力 | 现状 |
|---|---|
| 手机登录/登出 | ✅ `RealUserNetworkApi` 已实现 116/217/102，smoke 实测通过 |
| 连接目标配置 | ✅ 真实入口（`src/main.cpp`，原 `realnetworkmain`）已支持 host/port 参数（默认 SERVER_IP/SERVER_PORT） |
| 用户资料查询 | ❌ v2.4 无专用消息，当前返回 unsupported-protocol |
| 昵称修改 | ❌ 同上（v2.5 有 118/228 但协议未合入） |
| 余额展示 | ⚠️ 217 载荷已含 balanceCents，会话内未独立刷新 |
| 钱包充值/支付 | ❌ `IWalletNetworkApi` 合同已冻结，仅有 Mock |
| 服务端推送 | ❌ 223/225/226 无分发链路 |

## 2. 分阶段实施

### P0 远程连接打通（半天）

- 真实入口（`src/main.cpp`，原 `realnetworkmain`）增加 `CHARGER_SERVER_HOST/PORT`
  环境变量读取（优先于 JSON 参数），
  Key 注入同款模式，不落盘；
- 先用 `tools/network-smoke --host <远程IP>` 验证 107/230 心跳与 116/217 登录，
  再启动客户端——把"不通"隔离在网络层之前。
- 提交：`feat(app): support remote server targeting via environment`

### P1 用户资料真实查询（1 天）

- `IUserNetworkApi::queryCurrentUser` 真实化：`100 GETDATA {table:"user",
  cond:{username}}` → 200 DATA 取行映射 `UserProfileResult`
  （username/phone/nickname/status/balanceCents；user 表已脱敏，无 password）；
- 字段宽容解析 + 坏行防御，模式与 `RealChargerService` 一致；
- 全局单在途 + 两步内单表，规避 200 DATA 无回显歧义；
- `updateNickname` 维持 unsupported-protocol：v2.4 无消息，**不伪造成功**；
  预留 `UserBackendCapabilities`（仿 `chargingbackendcapabilities.h`），
  服务端升 v2.5 后按能力位开启 118/228。
- 提交：`feat(user): query user profile via getdata` / `test(user): cover profile correlation`
- 注意：GETDATA 返回的是数组，cond 只能期望服务端过滤，客户端必须仍按
  username==目标 自行校验唯一行（同 RealChargerService 详情过滤策略）。

### P2 钱包真实化（1-2 天，衔接队友 wallet 模块）

- 新增 `RealWalletNetworkApi : IWalletNetworkApi`（置于 src/network，同层理由）：
  - `queryWallet`：余额以最近一次 217/216 载荷为准 + GETDATA user 兜底刷新；
  - `recharge`：113 RECHARGE_REQ `{username, amountCents}` → 216；**变更操作**
    —— 携带 operationId，超时/断线 → ResultUnknown，禁止盲目重试，
    `queryOperationResult` 按 operationId 恢复（GETDATA walletTransaction 按
    username+金额+时间窗核对，或依赖服务端幂等）；
  - `payOrder`：115 PAY_REQ `{orderNo, username}` → 215，同样 ResultUnknown 纪律；
- 金额一律整数分过境，元仅作展示转换（Binder 层）；
- 提交：`feat(wallet): implement real wallet network api` / `test(wallet): ...`

### P3 服务端推送接入会话（1 天）

- 在 `BackendClient::frameReceived` 之上建轻量分发器（src/network/）：
  - 217/216/215 更新会话内余额快照；
  - 223 PAYMENT_NOTICE → 用户待支付提醒；
  - 225 CHG_PROGRESS → 充电进度（衔接 charging 模块）；
  - 226 CHG_FAULT_NOTICE → 异常结算提示（settled 字段决定是否刷新订单）；
- 推送无请求关联，按 orderNo/username 归属，找不到归属则丢弃并计数；
- 提交：`feat(user): dispatch server push notices into session`

### P4 装配与联调（半天）

- 真实入口（`src/main.cpp`，原 `realnetworkmain`）装配顺序：transport → backend →
  real APIs（user/wallet）→ services → binders；Mock 仅在 `user_demo` 入口保留；
- 远程联调清单：远程主机网络可达 → smoke 双通过 → 登录 → 资料刷新 →
  充值（含断线 ResultUnknown 恢复）→ 推送可见；
- 未验证项如实记录（同 bitdev 清单格式）。

## 3. 纪律红线（与既有合同一致）

1. 变更操作（充值/支付）必有 operationId；超时=ResultUnknown，恢复只能按原
   operationId 查询，不得自动重发；
2. 只读查询永不携带 operationId/ResultUnknown；
3. 金额整数分过境；协议常量只在 src/network 内使用；
4. 字段映射以 `contract:` 提交校准，不与功能提交混在一起；
5. 服务端 IP/端口不写入仓库（环境变量/运行参数注入）。

## 4. 测试策略

- 每个真实适配器配 FakeTransport+真实 BackendClient 回环测试
  （参照 `tests/charger/real-charger-service-tests.cpp` 全套场景）；
- 变更操作单列 ResultUnknown 恢复用例：超时→查询→命中/不命中/仍未知三分支；
- 回归基线：全仓 9 套件 + 新增套件全绿，`-Wall -Wextra` 0 警告。

## 5. 待确认问题（联调前需服务端答复）

1. user 表 GETDATA 是否支持 cond 精确过滤（不支持则全表拉取+客户端过滤，实训规模可接受）；
2. 216/215 应答是否回显 requestId/operationId（决定恢复查询的实现方式）；
3. 服务端是否已有 113 幂等保护（重复充值防重）；
4. walletTransaction 表字段（流水查询如果要做）。

## 6. 远程联机流程（P0 已实现，2026-09-08）

目标地址优先级：`--server-host/--server-port` 参数 > `CHARGER_SERVER_HOST/PORT`
环境变量 > 协议内置默认（127.0.0.1:12345）。已在本机验证两级解析。

1. 远程主机：启动服务端进程，确认监听 `0.0.0.0:12345`（防火墙放行）；
2. 客户端连通性：`ping <远程IP>` → `timeout 3 bash -c 'cat < /dev/null > /dev/tcp/<远程IP>/12345' && echo OK`；
3. 协议冒烟（不启动 GUI）：`./network-smoke --host <远程IP> --port 12345 --timeout 10000`，
   预期心跳 107/230 双通过；
4. 构建真实入口：`qmake CONFIG+=real_network && make -j4` → `bin/ChargingUserUI`；
5. 启动（两种方式任选）：
   - `export CHARGER_SERVER_HOST=<远程IP>; export CHARGER_SERVER_PORT=12345; ./bin/ChargingUserUI`
   - `./bin/ChargingUserUI --server-host <远程IP> --server-port 12345`
6. 启动日志应出现 `Starting real-network entry for <远程IP>:12345.` 与
   `Network state: connected`；然后走手机号登录验证 116/217 真实链路；
7. 网络中断演练：拔线/关服务端 → 客户端应打印 `Network state: disconnected`
   并每 5 秒重连，恢复后自动回到 connected。
