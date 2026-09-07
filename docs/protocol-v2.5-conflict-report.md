# 协议 v2.5 与充电桩页功能分支冲突记录

状态：存在冲突，暂不替换仓库根目录 `protocol.h` 和 `协议与接口说明.md`

核对日期：2026-09-08

## 1. 核对基准

- 新协议：微信附件 `协议(1).zip`，文档标记为 v2.5（2026-09-07）。
- 原协议：仓库根目录 `protocol.h`、`协议与接口说明.md`。
- 功能基准：`feature/station-detail-business` 分支最新提交
  `7211373a8788899a74a302fe39401ced46b62877`（`docs: define station detail business contract`）。
- 实现核对：当前工作区的 `IChargingService`、`ChargingUiBinder`、确认页 ViewState 和相关测试。

附件 SHA-256：

- `协议(1).zip`：以原始附件为准；压缩包内 `protocol.h` 为
  `14b6482c5778a4c43db4df3529aeefe47b1903e01b388ecbda47dbeafe5a2550`。
- 压缩包内 `协议与接口说明.md` 为
  `520357d36ad0c9eaafa6f2e07d2a722f74638929de8f855aeb67568b78a5a324`。

## 2. 新协议的有效新增

v2.5 在原协议基础上新增了以下能力，方向上与客户端需求不冲突：

- `117/219`：一般注册；
- `118/228`：账户资料修改；
- `119/229`：电站与电桩组合查询；
- `124/231`：管理员维护电站与电桩；
- `306/307` 以及 `AUTH_FAIL`、`OP_FORBIDDEN`、`DUPLICATE`；
- `device` 会话角色、头像字段和更明确的权限矩阵。

其中 `119/229` 能替代客户端当前通过 `100/200` 全表查询后再本地过滤的临时方案，
但其字段合同尚不能直接满足功能分支。

## 3. 阻塞合入的冲突

### 3.1 站点和电桩缺少稳定 ID

功能分支已冻结并使用 `stationId + chargerId` 贯穿详情、选择、确认和启动流程；
刷新时也依赖 `chargerId` 保留或清除选择。

v2.5 的 `119/229` 仍只接受和返回 `stationName + chargerCode`。直接接入只能把名称或编号
临时冒充稳定 ID，与功能合同“展示字段不得作为业务身份”的约束冲突。

需要服务端明确以下二选一方案后才能接入：

1. 在数据库和 `119/229` 中新增并返回稳定的 `stationId`、`chargerId`；或
2. 正式冻结 `stationName`、`chargerCode` 为不可变且全局唯一的业务 ID，并明确重命名、迁移规则。

推荐方案 1。

### 3.2 启动充电缺少请求关联与幂等能力

功能分支的 `ChargingUiBinder` 为每次读取生成 `requestId`，为启动充电生成唯一
`operationId`；超时或断线进入 `ResultUnknown` 后，只能按原 `operationId` 查询权威结果。

v2.5 的 `108/208` 仍是：

```json
// 108
{ "username": "...", "chargerCode": "..." }

// 208
{ "orderNo": "...", "price": 1.5, "priceCents": 150, "startedAt": "..." }
```

它没有：

- 请求与成功/失败应答共同携带并回显的 `requestId`；
- 启动变更的 `operationId`；
- 服务端按 `operationId` 幂等执行的规则；
- 按 `operationId` 查询启动结果的请求/应答消息；
- 错误应答中的原请求类型或等价关联字段。

因此网络超时后客户端无法判断订单是否已经创建，也不能安全重试。当前实现已将真实启动能力
保持为不可用状态，不能仅替换协议头后启用。

### 3.3 覆盖 `protocol.h` 会回退分片重组安全上限

仓库现有 `protocol.h` 在提交 `98156e7` 中增加了：

```cpp
#define MAX_ASSEMBLED_MSG_SIZE MAX_MSG_SIZE
```

附件 v2.5 删除了该常量。当前网络解析端依赖此上限限制完整业务消息的分片重组资源；
直接覆盖会造成已修复问题的回退。新协议应保留该定义，且服务端、客户端取值必须一致。

## 4. 建议的协议修订

在 v2.5 基础上补齐以下内容后再更新客户端协议：

- `119/229` 增加稳定 `stationId`、`chargerId`，查询条件优先使用 ID；
- `108/208` 增加并回显 `requestId`、`operationId`、`stationId`、`chargerId`；
- `208` 返回稳定 `orderId`（若继续使用 `orderNo`，需明确其就是不可变订单 ID）、
  `priceCentsPerKwhSnapshot` 和统一时区的 `startedAt`；
- 新增“按 `operationId` 查询变更结果”的消息码及终态定义；
- 所有 2xx/3xx 应答回显 `requestId`；变更类错误同时回显 `operationId` 和原请求类型；
- 保留 `MAX_ASSEMBLED_MSG_SIZE`，并在协议说明中写明重组累计上限。

## 5. 本次处理结论

本次不覆盖仓库根目录协议文件。v2.5 的注册、账户、站点查询和管理扩展可作为后续合并基础，
但必须先解决第 3 节冲突。服务端协议冻结后，应同步更新 `protocol.h`、协议说明、真实网络适配器
和协议测试，不能只更新宏定义。
