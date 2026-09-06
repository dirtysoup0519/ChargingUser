# 基础网络通信：代码设计、运行与调试指南

本文说明 `feature/network-basic-communication` 分支新增的基础网络通信能力，覆盖
代码边界、运行流程、构建方式、断点调试和交互验证。目标环境统一为 BitDev / Ubuntu
22.04 / Qt 6.2.4，qmake 必须使用 `/usr/bin/qmake6`。

## 1. 功能范围

当前实现提供：

- TCP 连接、主动断开和每 5 秒重连；
- 发送队列和部分写入处理；
- 12 字节协议头解析、半包、粘包及 `400/401/402` 分片重组；
- 每 30 秒发送一次零载荷 `HEARTBEAT_REQ (107)`；
- 将 `HEARTBEAT_ACK (230)` 和业务响应交给上层；
- `PHONE_LOGIN_REQ (116)` / `PHONE_LOGIN_ACK (217)` 的真实适配；
- 独立 `network-smoke` 工具，用于无业务副作用的真实心跳联调；
- `CONFIG+=real_network` 可选正式入口，默认入口和 `user_demo` 不受影响。

当前不提供：

- UI 中的连接中、离线和重连状态展示；
- 客户端主动等待心跳应答并判死连接的独立超时；
- 已确认可用的普通用户资料查询、昵称修改协议；
- 真实登录后的完整首页/资料页跳转接线；
- 对会改变业务数据的请求做自动重试。

因此，真实服务器心跳成功只能证明网络和协议闭环成功，不能表述为完整登录成功。

## 2. 相关文件及职责

| 文件 | 设计职责 |
|---|---|
| `protocol.h` | 定义帧头长度、消息码、心跳周期、重连周期和默认服务器地址。 |
| `massagehandler.h/.cpp` | 纯协议层；打包 JSON、解析字节流、处理半包/粘包和大数据分片，不操作 socket。 |
| `src/network/inetworktransport.h` | 网络传输抽象；上层只依赖连接、断开、发送和数据/错误信号。 |
| `src/network/qtnetworktransport.h/.cpp` | `QTcpSocket` 实现；管理连接和 FIFO 发送队列，断线时清除未发送数据。 |
| `src/network/backendclient.h/.cpp` | 网络生命周期协调器；维护连接状态、心跳定时器、重连定时器，并把协议帧交给业务适配器。 |
| `src/network/realusernetworkapi.h/.cpp` | 将用户领域请求映射为服务端消息码，管理请求超时和响应关联。 |
| `src/realnetworkmain.cpp` | 可选真实入口；装配 transport、backend、真实 API、应用层和登录窗口。 |
| `ChargingUser.pro` | 通过 `CONFIG+=real_network` 选择真实入口，并禁止和 `user_demo` 同时启用。 |
| `tools/network-smoke/` | 命令行联调工具；连接后发送一次心跳，可选发送手机号登录请求。 |
| `tests/network/protocol-framing-tests.cpp` | 验证帧头、半包、粘包、分片、非法长度和解析状态重置。 |
| `tests/network/network-transport-tests.cpp` | 验证真实 socket、部分写入、发送顺序、心跳、断线和 shutdown。 |
| `tests/network/network-adapter-tests.cpp` | 验证用户请求/响应映射、错误、超时、迟到应答和请求关联。 |
| `docs/network-basic-communication-integration-results.md` | 真实服务器 ELF 的环境、心跳闭环和故障行为记录。 |

## 3. 运行流程

### 3.1 建立连接

1. `realnetworkmain.cpp` 解析 `--server-host` 和 `--server-port`；
2. 按 `QtNetworkTransport → BackendClient → RealUserNetworkApi →
   UserApplicationAssembly` 的顺序创建对象；
3. `BackendClient::start()` 把状态改为 `Connecting`，再调用
   `QtNetworkTransport::connectToServer()`；
4. `QTcpSocket::connected` 到达后，transport 先刷新发送队列，再发出
   `connected`；
5. `BackendClient::handleConnected()` 重置旧解析状态，把状态改为
   `Connected`，启动心跳定时器。

对象使用栈逆序析构，因此 application assembly 和业务 API 会先于 backend 和
transport 销毁，非拥有指针在使用期间保持有效。应用退出时还会先调用
`BackendClient::shutdown()` 停止心跳和重连。

### 3.2 发送数据

1. 上层调用 `BackendClient::sendFrame(type, payload)`；
2. 心跳使用 `MassageHandler::makeHeartbeat()`，产生 12 字节帧头和零长度载荷；
3. 其他消息使用 `MassageHandler::pack()` 序列化 JSON；
4. `QtNetworkTransport::send()` 把字节追加到本地 FIFO 队列；
5. `flushWriteQueue()` 持续调用 `QTcpSocket::write()`；返回 0 时等待
   `bytesWritten` 后继续，部分接受时保留剩余字节；
6. 断线、错误或 shutdown 会清空尚未发送的队列，防止旧请求在新连接上重放。

`send()` 返回 `true` 的含义是“数据已被本地发送队列接受”，不是“服务端已经处理”。

### 3.3 接收与拆包

1. `QTcpSocket::readyRead` 读取当前可用字节并发出 `dataReceived`；
2. `MassageHandler::feed()` 将数据追加到接收缓冲；
3. 缓冲不足 12 字节时等待下一批数据；
4. 读取大端序的 4 字节类型和 8 字节长度；
5. 载荷未到齐时保留半包，载荷到齐后继续解析缓冲中的下一帧；
6. 普通帧直接发出 `frameReady`；`400/401/402` 先重组，再以原消息码交付；
7. `BackendClient` 只在 `Connected` 状态向业务层发出 `frameReceived`。

非法单帧长度会清空接收和分片状态，并产生 `ILLEGAL_REQUEST (301)`，避免错误字节
继续污染下一条消息。

### 3.4 断线与重连

- 已连接 socket 断开：`Connected → Reconnecting`；
- 5 秒定时器到期：`Reconnecting → Connecting`，再次连接；
- 首次连接被拒：上报 `networkError`，进入 `Reconnecting`；
- 重连成功：进入 `Connected`，清理旧半包并恢复心跳；
- 调用 `shutdown()`：停止两个定时器、清理解析/发送状态并进入
  `Disconnected`，迟到的连接成功不会重新启动状态机。

### 3.5 手机号登录

登录窗口只向 `IUserUiBinder` 发出登录意图。调用链为：

```text
LoginWindow
  -> IUserUiBinder
  -> UserService / AppFlowCoordinator
  -> RealUserNetworkApi
  -> BackendClient
  -> QtNetworkTransport
  -> server
```

服务端 `217` 响应沿反方向返回。当前服务端协议没有完整资料查询和昵称修改接口，
所以真实入口可能收到 `ProfileEdit` 或 `Home` 导航请求，但只记录警告，不伪造资料
或强行跳转。完整页面接线属于后续 UI 协作阶段。

## 4. 在 BitDev 中构建

### 4.1 真实网络入口

在 BitDev 终端执行：

```bash
repo=/mnt/hgfs/WorkSpace/QtProjects/APP/ChargingUser
build_dir=$(mktemp -d /tmp/charginguser-real-debug.XXXXXX)
cd "$build_dir"
/usr/bin/qmake6 "$repo/ChargingUser.pro" \
  CONFIG+=real_network CONFIG+=debug CONFIG-=release
make -j2
```

工程把正式产物写入：

```text
/mnt/hgfs/WorkSpace/QtProjects/APP/ChargingUser/bin/ChargingUserUI
```

运行：

```bash
QT_MESSAGE_PATTERN='[%{time hh:mm:ss.zzz}] %{type} %{function}: %{message}' \
  "$repo/bin/ChargingUserUI" \
  --server-host 127.0.0.1 \
  --server-port 12345
```

参数错误返回 2。例如：

```bash
"$repo/bin/ChargingUserUI" --server-port 0
echo $?
```

### 4.2 Demo 和默认入口

真实网络入口是可选构建，不会替换其他入口：

```bash
# 原默认入口
/usr/bin/qmake6 "$repo/ChargingUser.pro"

# Mock 完整流程 Demo
/usr/bin/qmake6 "$repo/ChargingUser.pro" CONFIG+=user_demo
```

不要同时传入 `CONFIG+=user_demo CONFIG+=real_network`；工程会主动报错，避免两个
`main()` 或两套运行语义混在一起。

### 4.3 Qt Creator

1. 在 BitDev 的 Qt Creator 中打开 `ChargingUser.pro`；
2. Kit 选择 Qt 6.2.4，确认 qmake 为 `/usr/bin/qmake6`；
3. 在 qmake Additional arguments 中填写 `CONFIG+=real_network`；
4. Debug 构建配置使用 `CONFIG+=debug CONFIG-=release`；
5. Run arguments 填写 `--server-host 127.0.0.1 --server-port 12345`；
6. 运行环境可加入上面的 `QT_MESSAGE_PATTERN`，便于把日志对应到函数。

切换 `real_network` 或 `user_demo` 后必须重新运行 qmake，不能只执行 make。

## 5. 断点调试

推荐按调用链设置以下断点：

| 断点 | 主要观察内容 |
|---|---|
| `BackendClient::start` | `m_started`、初始 `m_state`、transport 是否已经连接。 |
| `QtNetworkTransport::connectToServer` | `m_host`、`m_port`、socket 当前状态。 |
| `BackendClient::setState` | 每次状态转换的旧值和新值。 |
| `QtNetworkTransport::send` | 本次数据长度和 `m_pendingWrite`。 |
| `QtNetworkTransport::flushWriteQueue` | `accepted` 返回值、剩余队列长度和 socket 状态。 |
| `MassageHandler::feed` | 每次网络到达的数据长度、`recvBuf` 原长度。 |
| `MassageHandler::tryParseFrames` | `msgType`、`msgSize`、缓冲是否足够。 |
| `MassageHandler::dispatchChunk` | `assembling`、`assemblingType`、累计分片长度。 |
| `BackendClient::handleDisconnected` | 心跳是否停止、是否启动重连定时器。 |
| `BackendClient::attemptReconnect` | 是否处于 started 状态、重连前状态。 |
| `RealUserNetworkApi::startRequest` | 请求类型、requestId、是否已有同类请求。 |
| `RealUserNetworkApi::handleFrame` | 响应消息码、requestId 匹配和必填字段校验。 |

命令行 GDB 示例：

```bash
gdb --args "$repo/bin/ChargingUserUI" \
  --server-host 127.0.0.1 --server-port 12345
```

进入 GDB 后：

```gdb
set breakpoint pending on
break BackendClient::setState
break QtNetworkTransport::flushWriteQueue
break MassageHandler::dispatchChunk
break RealUserNetworkApi::handleFrame
run
```

常用检查：

```gdb
print m_state
print m_pendingWrite.size()
print recvBuf.size()
print assemblingBuf.size()
continue
```

若断点无法命中，先确认使用的是带 `CONFIG+=real_network CONFIG+=debug
CONFIG-=release` 重新 qmake 后生成的产物，而不是先前的 release 或 Demo 产物。

## 6. 通过命令行验证功能

### 6.1 构建 smoke test

```bash
repo=/mnt/hgfs/WorkSpace/QtProjects/APP/ChargingUser
smoke_build=$(mktemp -d /tmp/charginguser-smoke.XXXXXX)
cd "$smoke_build"
/usr/bin/qmake6 "$repo/tools/network-smoke/network-smoke.pro"
make -j2
./network-smoke --help
```

退出码：

| 退出码 | 含义 |
|---:|---|
| 0 | 收到合法的预期响应。 |
| 2 | 参数非法。 |
| 3 | TCP 连接或传输错误。 |
| 4 | 外部总超时。 |
| 5 | 服务端错误响应或响应结构不合法。 |
| 6 | 请求未能进入本地发送队列。 |

### 6.2 使用真实服务器验证心跳

服务器 ELF 会在当前目录创建数据库文件。为避免修改已有数据，先复制到隔离目录：

```bash
repo=/mnt/hgfs/WorkSpace/QtProjects/APP/ChargingUser
server_dir=$(mktemp -d /tmp/charginguser-server.XXXXXX)
cp "$repo/build/network-input-review/protocol-package/DatabaseSideProject" \
  "$server_dir/DatabaseSideProject"
chmod 700 "$server_dir/DatabaseSideProject"
cd "$server_dir"
./DatabaseSideProject
```

`build/network-input-review/` 属于 `.gitignore` 覆盖的本地输入材料，不随仓库提交。
如果上述 ELF 不存在，应先从团队确认的最新协议包重新解压，并核对 SHA-256；不要从
来源不明的二进制继续联调。本次已验证 ELF 的哈希见实机联调记录。

在服务器窗口点击 `start DatabaseServer`，确认界面显示服务器已启动。另开终端：

```bash
./network-smoke --host 127.0.0.1 --port 12345 --timeout 10000
echo $?
```

期望关键输出：

```text
Connected.
Sent message type=107 payloadBytes=0.
Received message type=230 payloadBytes=2.
Heartbeat round trip completed.
```

期望退出码为 0。2 字节响应载荷是服务端返回的 JSON 空对象 `{}`。

### 6.3 验证连接拒绝和重连

在服务器窗口点击 `stop`，然后再次运行 smoke test：

```bash
./network-smoke --host 127.0.0.1 --port 12345 --timeout 3000
echo $?
```

期望看到 `Transport error: Connection refused`，退出码为 3。

再运行真实入口，保持窗口开启，然后反复启动/停止服务器。控制台应出现类似状态：

```text
Network state: connecting
Network state: connected
Network error: The remote host closed the connection
Network state: reconnecting
Network state: connecting
```

服务恢复监听后，下一轮重连应重新进入 `connected`。

### 6.4 可选验证手机号登录

`network-smoke --phone` 可能使服务端自动注册未知手机号。只应对隔离的临时数据库
或明确获准的测试数据库使用：

```bash
./network-smoke \
  --host 127.0.0.1 \
  --port 12345 \
  --timeout 10000 \
  --phone 13800000000
```

工具会先完成心跳，再发送 `116`。期望收到 `217` 且响应包含非空 `username`。
日志只显示掩码手机号和载荷长度，不输出完整业务载荷。

也可以运行 `ChargingUserUI`，在登录窗口输入测试手机号并点击登录。此操作用于观察
`116/217` 和应用层状态，不应期待完整首页/资料页导航；控制台会明确记录尚未接线的
导航目标或资料协议不可用。

## 7. 自动化测试

三个网络测试程序的当前期望结果为 40/40。每个工程使用独立构建目录：

```bash
repo=/mnt/hgfs/WorkSpace/QtProjects/APP/ChargingUser

run_qtest() {
  name="$1"
  pro="$2"
  build_dir=$(mktemp -d "/tmp/$name.XXXXXX")
  cd "$build_dir"
  /usr/bin/qmake6 "$repo/$pro"
  make -j2
  "./$name"
}

run_qtest protocol-framing-tests tests/network/protocol-framing-tests.pro  # 12 项
run_qtest network-transport-tests tests/network/network-transport-tests.pro # 13 项
run_qtest network-adapter-tests tests/network/network-adapter-tests.pro     # 15 项
```

完整项目回归还应包括 user、flow、app、demo 和 map 测试，当前总数为 119。

QTest 可以只运行一个测试函数。在对应测试工程的独立构建目录完成 qmake 和 make 后，
执行例如：

```bash
./protocol-framing-tests oversizedFrameResetsPartialAssembly
./network-transport-tests transportContinuesAfterZeroAndPartialWrites
./network-transport-tests shutdownStopsReconnectAndRejectsLateConnect
```

## 8. 常见问题定位

| 现象 | 检查顺序 |
|---|---|
| 一启动就 `Connection refused` | 确认服务器窗口已点击 start；使用 `ss -ltnp '( sport = :12345 )'` 检查监听。 |
| 一直 `connecting` | 检查 host/port、虚拟机网络和 `transportError`；在 `setState` 断点观察是否进入重连。 |
| 已连接但收不到帧 | 检查服务端是否回包，再依次断在 `readyRead`、`feed`、`tryParseFrames`。 |
| 收到数据但业务无响应 | 检查 `frameReceived` 的消息码和 `RealUserNetworkApi::handleFrame` 的 requestId/必填字段。 |
| 心跳载荷显示 2 字节 | 请求 107 是零字节；实测服务端 230 返回 `{}`，即 2 字节，属于当前兼容行为。 |
| 修改构建开关后入口没变化 | 清理对应构建目录并重新 qmake；确认没有同时启用 `user_demo`。 |
| 登录后没有进入完整页面 | 这是当前明确限制；资料协议和真实页面交接尚未完成，不要在 UI 中伪造成功。 |

## 9. 复查发现与已知风险

截至 2026-09-06，完整自动化回归和真实服务器心跳闭环均通过，但代码复查发现一个
需要后续修复的分片资源边界问题：

- 解析器把单个物理帧限制为 `MAX_MSG_SIZE`（8 MB）；
- `BIGDATA_START/MID/END` 的每块大小没有按 `BIGDATA_THRESHOLD`（16 KB）校验；
- `assemblingBuf` 的累计大小没有上限；
- 异常对端可以持续发送单块合法的 `BIGDATA_MID`，导致重组缓冲持续增长。

建议修复时同时完成：

1. 冻结“完整业务消息”的最大长度；
2. 校验 START 不超过 `MSG_TYPE_LEN + BIGDATA_THRESHOLD`，MID/END 不超过
   `BIGDATA_THRESHOLD`；
3. 追加分片前检查累计长度，超限时 `resetAssembly()` 并上报一次协议错误；
4. 增加累计超限、超大单块、超限后正常帧恢复等测试；
5. 明确发送端 `pack()` 对超过完整消息上限的返回合同。

在该边界修复前，不应把当前实现用于接收不可信来源的无限分片流。其他已知限制包括
发送队列尚无全局背压上限、正式 UI 不展示连接状态，以及协议没有稳定的通用
requestId 回传约定。`BackendClient::setReconnectIntervalMs()` 也未像心跳间隔 setter
一样拒绝非正数；当前生产入口使用协议默认的正数值，测试和未来配置代码仍必须避免
传入 0 或负数，后续可补充参数校验和对应单元测试。
