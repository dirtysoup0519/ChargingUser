# 用户流程小 Demo

该 Demo 使用 `MockUserNetworkApi + UserApplicationAssembly + IUserUiBinder`，
不依赖服务端，并通过 `CONFIG+=user_demo` 切换到独立 Demo 入口。

## 构建

在 BitDev / Ubuntu 22.04 / Qt 6.2.4 中执行：

```bash
mkdir -p build/user-demo
cd build/user-demo
qmake6 ../.. CONFIG+=user_demo
make -j2
../../bin/ChargingUserUI
```

## 演示账号

演示用户、测试输入和模拟失败规则统一维护在
`src/demo/user-demo-data.tmp`，不要在 C++ 测试或 Demo 控制器中新增硬编码用户数据。

- `13800000000`：普通老用户，登录后进入首页。
- `13900000000`：Frozen 老用户，登录后进入受限首页。
- `13600000000`：账号状态 Unknown，登录后进入受限首页。
- `13700000000`：第一次登录模拟网络超时，再次点击登录成功。
- 其他合法的 11 位手机号：本次 Demo 运行中首次按新用户处理，自动生成默认昵称后进入资料完善页；保存昵称后进入首页。退出后再次登录同一手机号会按老用户进入首页，并保留已确认保存的昵称。

退出后可以换号重新演示。登录、昵称保存和资料刷新均有 450ms 模拟延迟，用于观察 Loading 状态。

资料编辑页还可输入以下特殊昵称检查异常展示：

- `网络错误`：显示 NetworkError。
- `服务错误`：显示 ServerError。
- `结果未知`：先显示 ResultUnknown，再由资料刷新确认结果。

## 当前版本状态与未完成项

本次合并基于 `user-design-main@56c8f3c` 与
`user-module-main@311f6bc`。当前提交是调试候选版本，不应视为最终验收版本。

已完成：

- 用户服务、流程协调器、无 QWidget Binder、Mock Demo 装配已接入现有页面。
- Debug Demo 必须在 BitDev（Ubuntu 22.04、Qt 6.2.4）使用 `qmake6` 编译和验证。
- 用户模块 30/30、流程层 20/20、应用/Binder 11/11 已在 BitDev / Qt 6.2.4
  中重新构建并通过；网络层 15/15 为此前合并验证结果，本次未改动网络层。
- Demo 页面交互测试覆盖登录校验、老/新用户分流、新用户注册后重登、昵称修改后重登、
  页面栈跳转、网络失败重试、Frozen 展示和退出，共 13 个测试项，本次全部通过。

本次已复查问题：

- 退出返回登录页会清空手机号，已由页面交互测试覆盖。
- 新注册手机号在同一次 Demo 运行中退出再登录时按老用户处理，并保留服务端模拟成功确认的昵称；
  普通用户修改昵称后重登也会保留新昵称。
- 站点详情、路线导航和钱包充值页在主窗口首次显示前已注册到页面栈，不再覆盖登录后的首页。

尚未实现或验收：

- ~~正式 `src/main.cpp` 仍只是 UI 入口~~ **2026-09-08 更新**：正式入口
  `src/main.cpp`（原 `realnetworkmain.cpp` 改名）已装配真实
  `UserApplicationAssembly` 与全部页面；完整用户流程由默认构建提供，
  `CONFIG+=user_demo` 仅作离线演示/视觉验收。
- 服务端协议 v2.4 尚无普通用户资料查询、昵称修改的专用消息，真实后端链路未完成；
  Demo 使用 `MockUserNetworkApi`，不能作为真实网络联调结论。
- 尚未完成有桌面显示环境下的逐页人工点击、截图、长文案/小窗口视觉验收；
  当前自动交互测试使用 Qt `offscreen` 平台。
