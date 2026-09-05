# 用户流程小 Demo

该 Demo 使用 `MockUserNetworkApi + UserApplicationAssembly + IUserUiBinder`，不依赖服务端，且不会替换正式 `src/main.cpp`。

## 构建

在 BitDev / Ubuntu 22.04 / Qt 5.15.3 中执行：

```bash
mkdir -p build/user-demo
cd build/user-demo
qmake ../.. CONFIG+=user_demo
make -j2
../../bin/ChargingUserUI
```

## 演示账号

- `13800000000`：普通老用户，登录后进入首页。
- `13900000000`：Frozen 老用户，登录后进入受限首页。
- `13600000000`：账号状态 Unknown，登录后进入受限首页。
- `13700000000`：第一次登录模拟网络超时，再次点击登录成功。
- 其他合法的 11 位手机号：首次按新用户处理，自动生成默认昵称后进入资料完善页；保存昵称后进入首页。

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
- Debug Demo 已在 BitDev（Ubuntu 22.04、Qt 5.15.3）成功编译。
- 用户模块 30/30、流程层 20/20、网络层 15/15、应用/Binder 11/11
  曾在本次合并过程中通过。
- Demo 页面交互测试覆盖登录校验、老/新用户分流、网络失败重试、Frozen
  展示和退出，共 8 个测试项。

已知待复查问题：

- 首轮 Demo 页面交互测试为 7/8，通过后发现“退出返回登录页时手机号未清空”。
  当前代码已将 Binder 的“显式空字符串”定义为清空命令，并修正登录页渲染逻辑，
  但用户要求先提交，因此该修复尚未在目标虚拟机复跑。
- 最新两处退出清空修复之后，Debug Demo、应用/Binder 测试和 Demo 页面交互测试
  均需重新构建执行一次，确认无编译回归且达到 8/8。

尚未实现或验收：

- 正式 `src/main.cpp` 仍只是 UI 入口，尚未装配真实 `UserApplicationAssembly`
  和页面导航；完整用户流程目前仅由 `CONFIG+=user_demo` 入口提供。
- 服务端协议 v2.4 尚无普通用户资料查询、昵称修改的专用消息，真实后端链路未完成；
  Demo 使用 `MockUserNetworkApi`，不能作为真实网络联调结论。
- 尚未完成有桌面显示环境下的逐页人工点击、截图、长文案/小窗口视觉验收；
  当前自动交互测试使用 Qt `offscreen` 平台。
