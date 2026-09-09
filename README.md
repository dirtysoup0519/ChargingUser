# ChargingUser（智充用户端）

ChargingUser 是基于 **Qt 6 / C++17** 开发的充电服务用户端。客户端通过自定义 TCP
协议连接服务端，提供用户登录、电站查询、腾讯地图、扫码充电、预约、订单、结算和钱包
等完整页面流程；同时保留不依赖服务端的 Demo 数据入口，便于离线演示和 UI 回归。

> 目标运行环境：VMware BitDev，Ubuntu 22.04，Qt 6.2.4。
> Windows 宿主机主要用于编辑和 Git 操作，正式构建与验收应在 BitDev 中完成。

## 功能概览

- 手机号免密登录/自动注册、用户名密码登录、用户资料和头像维护；
- 登录页动态切换服务端 IP 与端口；
- 电站列表、搜索、详情、充电桩状态及手动/自动刷新；
- 腾讯地图、定位、地址搜索和路线规划，缺少 WebEngine 时提供降级地图；
- 摄像头实时扫码和从相册识别二维码；
- 充电确认、启动、进度同步、停止、结算和钱包支付；
- 预约创建、活动预约恢复、取消预约和预约限制；
- 充电订单、预约订单、订单详情、钱包余额及资金流水；
- TCP 心跳、断线重连、分片重组、超时处理和服务端推送；
- 正式网络入口与 fixture-backed Demo 共用同一套页面组件。

## 运行流程

```text
登录/注册
  └─ 首页（电站列表与地图）
      ├─ 电站详情 → 导航
      ├─ 电站详情 → 扫码 → 充电确认 → 正在充电
      │                                  └─ 停止 → 结算 → 支付
      ├─ 电站详情 → 预约 → 支付 → 活动预约
      └─ 我的 → 资料 / 钱包 / 订单 / 常用电站
```

底部“充电”入口打开扫码页，不直接打开空的“正在充电”页面。正在充电页只会在启动成功、
恢复活动订单或从订单列表查看活动订单时进入。

## 技术架构

项目使用 ViewState + Binder 将 QWidget、业务逻辑和网络协议解耦。

```text
presentation/pages     页面输入、意图信号和状态渲染
        ↓ ↑
app                     页面与业务服务的装配、ViewState 映射
        ↓ ↑
modules                 用户、地图、电桩、充电、订单、钱包、预约业务
        ↓ ↑
network                 TCP 传输、协议适配、请求关联和服务端推送
        ↓ ↑
massagehandler          12 字节帧头、拆包、粘包及分片重组
```

主要目录：

| 路径 | 作用 |
|---|---|
| `src/presentation/` | 页面、展示合同和可复用 Widget |
| `ui/` | Qt Designer `.ui` 文件 |
| `styles/` | 全局 QSS 主题 |
| `resources/` | 图片、图标、地图 HTML、Qt 资源文件 |
| `src/app/` | UI 与领域服务的装配和 Binder |
| `src/modules/` | 领域类型、接口、业务服务和 Mock 服务 |
| `src/network/` | TCP、真实协议适配器和推送分发 |
| `src/flow/` | 登录后页面分流和跨模块流程 |
| `src/demo/` | Demo fixture 与离线演示支持 |
| `tests/` | 按模块组织的 Qt Test 测试 |
| `tools/` | 摄像头和网络 smoke 工具 |
| `docs/` | 协议接入、模块协作和联调文档 |

更详细的边界说明见 [docs/project-structure.md](docs/project-structure.md)。

## 环境依赖

### 必需

- Ubuntu 22.04（项目验收环境）；
- GCC/G++，支持 C++17；
- GNU Make；
- Qt 6 Core、Widgets、Network；
- `qmake6`；
- `pkg-config`。

### 可选能力

| 依赖 | 提供的能力 | 缺失时行为 |
|---|---|---|
| Qt 6 Multimedia / MultimediaWidgets | 摄像头实时扫码 | 仍可从相册选择二维码 |
| ZXing C++ | 二维码内容解析 | 扫码识别不会编译进程序 |
| Qt 6 WebEngineWidgets / WebChannel | 腾讯 JS 地图 | 使用绘制式降级地图 |
| GStreamer、V4L2 | Linux 摄像头运行时 | 摄像头可能打开但收不到画面帧 |

Ubuntu 安装基础环境：

```bash
sudo apt update
sudo apt install -y \
  build-essential pkg-config \
  qt6-base-dev qt6-base-dev-tools qt6-tools-dev-tools \
  qt6-multimedia-dev qt6-webengine-dev \
  v4l-utils gstreamer1.0-plugins-base \
  gstreamer1.0-plugins-good gstreamer1.0-libav
```

ZXing 和部分 Qt 组件位于 Ubuntu `universe` 软件源：

```bash
sudo apt install -y software-properties-common
sudo add-apt-repository -y universe
sudo apt update
sudo apt install -y libzxing-dev
```

不同发行版的包名可能不同，安装前可查询：

```bash
apt-cache search zxing | grep -E 'dev|cpp'
apt-cache search webchannel | grep -i qt6
```

确认环境：

```bash
/usr/bin/qmake6 -query QT_VERSION
pkg-config --modversion zxing 2>/dev/null || \
  pkg-config --modversion zxing-cpp
ls -l /dev/video* 2>/dev/null
```

## 获取代码

```bash
git clone <repository-url> ChargingUser
cd ChargingUser
git switch main
```

团队日常开发分支为 `ui-last`，已验收内容再合并到 `main`。不要在有未提交修改时直接
切换或合并分支。

## 构建

### 正式网络入口（默认）

`ChargingUser.pro` 默认自动启用 `real_network`，无需额外添加配置。推荐使用独立构建目录：

```bash
repo=/mnt/hgfs/WorkSpace/QtProjects/APP/ChargingUser
build_dir=$(mktemp -d /tmp/charginguser-real.XXXXXX)

cd "$build_dir"
/usr/bin/qmake6 "$repo/ChargingUser.pro" CONFIG+=debug CONFIG-=release
make -j"$(nproc)"
```

正式可执行文件固定输出到：

```text
/mnt/hgfs/WorkSpace/QtProjects/APP/ChargingUser/bin/ChargingUserUI
```

不要使用 Ubuntu 的 Qt 5 `/usr/bin/qmake`，也不要使用 Windows Anaconda 自带的 Qt 5
`qmake.exe`。

### 离线 Demo

Demo 不连接服务端，使用 Mock 服务和 `src/demo/*.tmp` fixture：

```bash
repo=/mnt/hgfs/WorkSpace/QtProjects/APP/ChargingUser
mkdir -p /tmp/charginguser-demo
cd /tmp/charginguser-demo

/usr/bin/qmake6 "$repo/ChargingUser.pro" CONFIG+=user_demo CONFIG-=real_network
make -j"$(nproc)"
./bin/ChargingUserUI
```

`user_demo` 与 `real_network` 互斥，不能同时启用。

### 确认二维码支持已编译

```bash
grep CHARGINGUSER_ENABLE_ZXING Makefile
```

正常情况下会看到 `-DCHARGINGUSER_ENABLE_ZXING`。Ubuntu 22.04 自带的旧版 ZXing 会额外
定义 `CHARGINGUSER_ZXING_LEGACY`。

## 正式入口运行

默认服务端为 `127.0.0.1:12345`：

```bash
./bin/ChargingUserUI
```

命令行指定服务端：

```bash
./bin/ChargingUserUI \
  --server-host 10.194.218.232 \
  --server-port 12345
```

也可以使用环境变量：

```bash
export CHARGER_SERVER_HOST=10.194.218.232
export CHARGER_SERVER_PORT=12345
./bin/ChargingUserUI
```

配置优先级：

```text
命令行参数 > CHARGER_SERVER_HOST/PORT > 127.0.0.1:12345
```

实训服务端没有完整“操作结果查询”能力时，程序默认使用项目当前的实训兼容策略。仅做
只读安全检查时可以添加：

```bash
./bin/ChargingUserUI --safe-operations-only
```

`--allow-unsafe-test-operations` 仅用于受控测试环境，禁止连接生产服务端使用。

## 腾讯地图配置

真实 Key 不得写入源码或提交到 Git。推荐复制本地配置：

```bash
cp config/tencent-map.example.json config/tencent-map.local.json
```

编辑 `config/tencent-map.local.json`：

```json
{
  "provider": "tencent",
  "key": "YOUR_TENCENT_MAP_KEY",
  "region": "北京市",
  "defaultAddress": "北京理工大学良乡校区",
  "defaultLocation": {
    "latitude": 39.731782,
    "longitude": 116.172130
  }
}
```

也可以临时使用环境变量：

```bash
export TENCENT_MAP_KEY='YOUR_TENCENT_MAP_KEY'
export TENCENT_MAP_REGION='北京市'
./bin/ChargingUserUI
```

虚拟机中 WebGL 不可用时尝试软件渲染：

```bash
export CHARGING_TENCENT_DISABLE_GPU=1
./bin/ChargingUserUI
```

程序不会把 Key 输出到日志，`config/tencent-map.local.json` 已被 `.gitignore` 排除。

## 扫码与摄像头

客户端接受以下二维码载荷：

```text
TC-22E2B-01
```

或：

```json
{"chargerCode":"TC-22E2B-01"}
```

也支持带 `chargerCode` 查询参数的 URL。扫码只提取电桩编号，电站、功率、状态和价格仍
由服务端查询确认，二维码本身不是可信业务数据。

检查虚拟机摄像头：

```bash
v4l2-ctl --list-devices
ls -l /dev/video*
```

将当前用户加入摄像头设备组后，需要注销并重新登录：

```bash
sudo usermod -aG video "$USER"
```

VMware 中必须将 USB 摄像头连接给 BitDev。摄像头指示灯亮但没有画面帧时，先运行：

```bash
repo=/mnt/hgfs/WorkSpace/QtProjects/APP/ChargingUser
build_dir=$(mktemp -d /tmp/charginguser-camera.XXXXXX)
cd "$build_dir"
/usr/bin/qmake6 "$repo/tools/camera-smoke/camera-smoke.pro"
make -j2
./camera-smoke
```

## 网络协议

协议定义集中在 `protocol.h`，帧处理位于 `massagehandler.h/.cpp`。

- 帧头：大端序 `4B msgType + 8B msgSize`；
- JSON：UTF-8 Compact；
- 单帧载荷上限：8 MiB；
- 大消息：使用 `400/401/402` 分片；
- 心跳：客户端每 30 秒发送一次；
- 重连：断线后按 5 秒周期尝试恢复；
- 金额：领域层统一使用整数“分”，仅展示层格式化为元；
- 客户端不直接访问数据库，所有状态以服务端响应为准。

主要消息包括手机号登录、资料更新、电站查询、订单查询、开始/停止充电、充值、支付、
预约、取消预约以及订单/预约状态推送。详细字段和错误码见
[协议与接口说明.md](协议与接口说明.md)。

## 测试

测试按模块放在 `tests/`：

- `tests/network/`：协议帧、TCP、请求队列和推送；
- `tests/user/`、`tests/flow/`：登录注册与页面流转；
- `tests/map/`、`tests/charger/`：地图、电站和电桩；
- `tests/charging/`、`tests/order/`：启动、停止、订单和订单 UI；
- `tests/wallet/`、`tests/reservation/`：钱包、流水和预约；
- `tests/app/`、`tests/demo/`：Binder 和完整 Demo 流程。

单项测试构建示例：

```bash
repo=/mnt/hgfs/WorkSpace/QtProjects/APP/ChargingUser
build_dir=$(mktemp -d /tmp/charginguser-test.XXXXXX)
cd "$build_dir"

/usr/bin/qmake6 "$repo/tests/network/protocol-framing-tests.pro"
make -j2
./protocol-framing-tests
```

切换到其他测试时应使用新的构建目录，避免不同 `.pro` 的对象文件相互污染。

## 网络联调与日志

建议打开结构化 Qt 日志：

```bash
export QT_MESSAGE_PATTERN='[%{time hh:mm:ss.zzz}] %{type} %{function}: %{message}'
./bin/ChargingUserUI --server-host 127.0.0.1 --server-port 12345 \
  2>&1 | tee /tmp/charginguser.log
```

只验证 TCP 和心跳时使用网络 smoke 工具：

```bash
repo=/mnt/hgfs/WorkSpace/QtProjects/APP/ChargingUser
build_dir=$(mktemp -d /tmp/charginguser-network.XXXXXX)
cd "$build_dir"
/usr/bin/qmake6 "$repo/tools/network-smoke/network-smoke.pro"
make -j2
./network-smoke --host 127.0.0.1 --port 12345
```

## 常见问题

### `Cannot read ... APP: file to open is a directory`

qmake 收到的是目录而不是 `.pro` 文件。请显式传入工程文件：

```bash
/usr/bin/qmake6 /mnt/hgfs/WorkSpace/QtProjects/APP/ChargingUser/ChargingUser.pro
```

### `qmake6: command not found`

```bash
sudo apt install qt6-base-dev qt6-base-dev-tools
which qmake6
/usr/bin/qmake6 -query QT_VERSION
```

### 构建成功但不能识别二维码

```bash
pkg-config --list-all | grep -i zxing
grep CHARGINGUSER_ENABLE_ZXING Makefile
```

若第二条没有输出，安装 ZXing 后必须重新运行 qmake，不能只执行 `make`。

### 摄像头已打开但画面为空

检查 `/dev/video*`、VMware USB 连接、`video` 用户组和 GStreamer 插件。先用
`tools/camera-smoke` 判断问题位于虚拟机设备层还是主程序页面层。

### 腾讯地图提示无法创建 WebGL 上下文

```bash
export CHARGING_TENCENT_DISABLE_GPU=1
```

如果仍失败，程序会保留文字电站列表和降级地图；路线与站点业务不应依赖 WebGL 才能
继续使用。

### 登录页仍显示旧服务器地址

检查命令行参数和环境变量：

```bash
echo "$CHARGER_SERVER_HOST"
echo "$CHARGER_SERVER_PORT"
```

清除旧环境变量后，默认会恢复为 `127.0.0.1:12345`：

```bash
unset CHARGER_SERVER_HOST CHARGER_SERVER_PORT
```

### Qt Creator 运行参数

在 Projects → Run → Command line arguments 中填写：

```text
--server-host 127.0.0.1 --server-port 12345
```

Qt Creator Kit 必须选择 Qt 6.2.4，并确保 qmake 为 `/usr/bin/qmake6`。

## 安全与协作约定

- 不提交腾讯地图 Key、密码、Token、证书、`.vm-credentials` 或本机配置；
- 变更操作使用 `operationId`，结果未知时先查询权威状态，不盲目重试；
- UI 不直接访问 Socket、JSON、数据库或协议消息码；
- Mock 数据只能存在于 Demo 和测试适配器中；
- `ui/`、`src/presentation/`、`src/app/` 和公共 ViewState 是协作接缝，修改时避免整文件覆盖；
- 切换正式/Demo配置后必须重新运行 qmake；
- Qt6构建产物、日志和本地配置不得提交。

## 相关文档

- [协议与接口说明](协议与接口说明.md)
- [项目目录与职责](docs/project-structure.md)
- [UI 与网络接入合同](docs/ui-network-integration-contract.md)
- [充电流程协作文档](docs/charging-flow-collaboration.md)
- [地图与导航协作文档](docs/map-navigation-collaboration.md)
- [网络构建与调试指南](docs/network-basic-communication-debug-guide.md)
- [腾讯地图集成说明](docs/tencent-map-frontend-integration.md)
- [Demo 使用说明](docs/user-demo.md)

## License

仓库当前未声明开源许可证。在明确许可证之前，请勿将代码作为开源项目再发布。
