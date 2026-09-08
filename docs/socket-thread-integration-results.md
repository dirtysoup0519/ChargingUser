# 客户端 Socket 工作线程阶段一至三验证记录

日期：2026-09-08

## 环境

- 虚拟机：BitDev
- 系统：Ubuntu 22.04
- Qt/qmake：Qt 6.2.4，`/usr/bin/qmake6`
- 共享源码：`/mnt/hgfs/WorkSpace/QtProjects/APP/ChargingUser`
- 服务端：用户提供的 `协议(1).zip/DatabaseSideProject`
- 服务端 SHA-256：`F46B47CF795D32273AE603107BE6384C4BE47E7B6E4832F1FFDC800053EA8962`

服务端 ELF 在 `/tmp/charginguser-stage3-server-screen` 的隔离目录运行，使用独立
`Server.db`，没有访问仓库或既有服务端数据库。由于客户机当时停在 GDM 登录界面，
服务端窗口运行于仅解压到 `/tmp`、未安装到系统的 Xvfb 虚拟显示。

## 自动化测试

`client-socket-thread-tests` 使用本地 `QTcpServer` 验证：

- Worker 线程启动、停止和重复启动；
- 未启动时拒绝发送；
- `QTcpSocket`、`BackendClient` 和 `MassageHandler` 位于 Worker 线程；
- 主线程异步发包；
- 半包、粘包在网络线程完成解析；
- 完整 JSON 帧回到 Manager 所在线程交付。

结果：5/5 通过。

既有 `network-transport-tests` 结果：13/13 通过。主工程使用 Qt 6.2.4 构建成功。

## 真实服务器闭环

心跳测试：

```text
Connected.
Sent message type=107 payloadBytes=0.
Received message type=230 payloadBytes=2.
Heartbeat round trip completed.
```

隔离数据库预置测试用户后，只读站点查询：

```text
Connected.
Sent message type=107 payloadBytes=0.
Received message type=230 payloadBytes=2.
Sent message type=116 for 199****0001; payload not logged.
Received message type=217 payloadBytes=151.
Sent read-only station query type=100; payload not logged.
Received message type=200 payloadBytes=11.
Heartbeat, login, and station query completed; rows=0.
```

测试只记录消息类型、载荷长度和返回条数，不记录完整手机号或业务载荷。

## 服务端已知问题

附件服务端在全新数据库中首次执行手机号免密登录自动注册时返回：

```text
302 / DB_ERROR: NOT NULL constraint failed: user.password
```

这说明该 ELF 的自动注册代码没有为 `user.password` 的 NOT NULL 字段提供值。为继续
验证传输层，本次仅在隔离数据库中预置测试用户；客户端线程通信本身能够正确接收并
解析该错误帧。正式使用手机号自动注册前，需要服务端修复或提前创建用户。

