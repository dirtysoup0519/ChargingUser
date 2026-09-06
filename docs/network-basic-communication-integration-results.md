# 基础网络通信实机联调记录

日期：2026-09-06
客户端基线：`bf0829e`（`feature/network-basic-communication`）

## 1. 结论

在 BitDev 中使用真实服务器 ELF 完成了客户端“连接、发送心跳、接收应答、
拆包解码”闭环。`network-smoke` 发送 `HEARTBEAT_REQ (107)`，收到
`HEARTBEAT_ACK (230)`，退出码为 0。

服务器主动停止和服务器进程异常退出后，真实网络入口都能观察到断开并进入
重连；端口未监听时，smoke test 按传输失败退出码 3 结束。

本次只验证无业务副作用的心跳。未发送手机号登录请求，未修改已有服务器数据库
或业务数据，也未验证资料查询和昵称修改。心跳成功不代表完整登录链路成功。

## 2. 验收环境

- 虚拟机：VMware `BitDev`，运行时动态确认只有一个实例；
- 客户机 IP：`192.168.81.129`（仅为本次动态查询结果，后续不得复用）；
- 系统：Ubuntu 22.04.3 LTS，x86_64；
- Qt：6.2.4；
- qmake：`/usr/bin/qmake6`，Qt 安装前缀 `/usr`；
- 共享源码：`/mnt/hgfs/WorkSpace/QtProjects/APP/ChargingUser`。

## 3. 服务器输入与依赖

本次选择更新时间较新的 `protocol-package/DatabaseSideProject`：

- 大小：8,044,240 字节；
- SHA-256：`8d51ece3824d8af61007f0652b646590f08a57d68927582d66d768cf9b70132a`；
- ELF：64 位 x86-64 PIE，动态链接，包含调试信息；
- 直接依赖：Qt 6 Widgets、Gui、Network、Sql、Core；
- `ldd` 未发现缺失依赖；
- 使用 `QT_QPA_PLATFORM=offscreen` 可启动，SQLite 驱动可创建隔离测试数据库。

服务器 ELF 被复制到 `/tmp` 下的独立临时工作目录运行。它会在当前工作目录创建
`Server.db`、备份和 WAL 相关文件，因此没有从原始目录直接启动，也没有接触已有
业务数据库。

服务器启动后默认只显示管理窗口，不会自动监听。通过窗口中的
`start DatabaseServer` 标准按钮动作启动服务后，实际监听地址为
`127.0.0.1:12345`，监听队列上限为 50。

## 4. 心跳闭环

构建命令：

```bash
/usr/bin/qmake6 \
  /mnt/hgfs/WorkSpace/QtProjects/APP/ChargingUser/tools/network-smoke/network-smoke.pro
make -s -j2
```

运行命令：

```bash
./network-smoke --host 127.0.0.1 --port 12345 --timeout 10000
```

关键输出：

```text
Connecting to 127.0.0.1:12345 (timeout 10000 ms)...
Connected.
Sent message type=107 payloadBytes=0.
Received message type=230 payloadBytes=2.
Heartbeat round trip completed.
```

退出码：`0`。

实测响应消息类型与 `protocol.h` 一致。心跳请求使用零长度载荷；服务端心跳应答
载荷为 2 字节 JSON 空对象 `{}`，现有解码器可以正常处理，本次未发现协议状态污染。

## 5. 故障行为

### 5.1 服务器主动停止

真实网络入口连接成功后，通过服务器窗口的 `stop` 按钮停止监听。客户端观察到：

```text
Network state: connecting
Network state: connected
Network error: The remote host closed the connection
Network state: reconnecting
Network state: connecting
Network error: Connection refused
Network state: reconnecting
```

说明已建立连接关闭后会进入重连，5 秒后的下一次连接尝试在停服状态下被拒绝，
并继续保持重连流程。

### 5.2 初始连接被拒绝

确认 `127.0.0.1:12345` 无监听后运行：

```bash
./network-smoke --host 127.0.0.1 --port 12345 --timeout 3000
```

输出为 `Transport error: Connection refused`，退出码为 `3`，符合 smoke test 的
`TransportFailure` 合同。

### 5.3 服务器异常退出

服务器和真实网络入口建立连接后，终止本次临时服务器进程。客户端先上报远端关闭，
随后依次进入 `reconnecting`、`connecting`；由于服务器已退出，下一次连接被拒绝后
再次进入 `reconnecting`。端口检查确认 `12345` 已无监听。

故障观察使用外部 `timeout` 限制为 12—13 秒，观察进程退出码 `124` 表示外部观察
窗口到期，不代表客户端自身崩溃或协议失败。

## 6. 未验证项与限制

- 未运行 `--phone`，因为未知手机号可能触发自动注册并改变业务数据；
- 未验证 `PHONE_LOGIN_REQ (116)` / `PHONE_LOGIN_ACK (217)` 的真实字段；
- 服务端仍未提供已确认的普通用户资料查询和昵称修改专用协议，真实登录后的完整
  页面链路标记为“未支持”；
- 未验证服务端非回环地址监听、跨主机通信和防火墙配置；
- 未验证需要 90 秒空闲窗口的服务端掉线判定；
- 服务器只有 ELF，没有对应版本的完整源代码，本次结论仅覆盖上述黑盒行为和哈希
  所标识的二进制版本。

## 7. 阶段判断

`network-basic-communication-plan.md` 第五阶段要求的环境检查、真实监听、无副作用
心跳闭环、主动停服、连接拒绝、异常断开和结果记录均已完成。基础网络通信分支的
第一至第五阶段可以进入提交复查；第六阶段 UI 状态展示仍不属于本分支。

## 8. 复验轮记录（2026-09-06，基线 98156e7）

分片边界修复（`98156e7` fix(protocol): bound chunk reassembly resources）合入后，
按《BitDev 实机联调操作清单》在 Qt 6.2.4 / /usr/bin/qmake6 环境完整复跑一轮，
替换此前基于 `bf0829e` 的结论。

### 8.1 环境

- 虚拟机、共享路径、服务器 ELF 与哈希（`8d51ece3...70132a`）均与首次联调一致；
- 服务器仍于 `/tmp/server-run` 隔离运行，工作目录未接触共享目录与原数据库；
- 客户端 IP 为动态获取（不复用历史记录值）。

### 8.2 结果

- **心跳闭环**：`network-smoke` 连续两次发送 `HEARTBEAT_REQ (107)`、收到
  `HEARTBEAT_ACK (230, payloadBytes=2)`，退出码均为 `0`；
- **自动化测试**：protocol-framing 16/16（含 4 项分片边界新回归）、
  network-transport 13/13、network-adapter 15/15，合计 44/44，零 FAIL，
  与沙箱 Qt 5.15.15 结果一致；
- **真实入口参数校验**：`--server-port 0` 报错退出码 `2`；
- **断线重连观察**：连接后终止服务器进程，客户端依次呈现
  `connected → reconnecting → connecting → Connection refused → reconnecting`
  两轮 5 秒重连；重新拉起服务器并点击 "start DatabaseServer" 后，
  客户端自动恢复至 `connected`，自愈闭环完整，未见旧连接报文污染或重连风暴。

### 8.3 结论与未验证项

首次联调第 7 节的阶段判断在 `98156e7` 基线上继续成立。新增未验证项：无。
分片边界修复使解析器可安全面对不可信分片流；发送队列背压、UI 连接状态展示、
`setReconnectIntervalMs()` 参数校验仍为已知限制（见调试指南 §9.1）。
