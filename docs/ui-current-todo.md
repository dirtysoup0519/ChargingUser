# 当前 UI 负责人待办（对接 M1 + M4）

> 分支：`user-design-main`。本文只列 UI 可独立完成的工作；`user-module-main` 已负责 M1 用户服务、网络适配和 M4 用户流程，不要复制或修改其 `src/modules/`、`src/network/`、`src/flow/` 实现。

> 2026-09-05 交接更新：小 Demo 优先于完整页面重构。逻辑分支当前提交为 `872e738`，已经提供 `MockUserNetworkApi`、`UserApplicationAssembly`、`IUserUiBinder`、`LoginViewState` 和 `ProfileEditViewState`，逻辑与网络测试共 75/75 通过。UI 先完成下面的“P0：小 Demo 最短路径”，完成后再继续其余 P1/P2。

## 当前可对接的逻辑能力

逻辑层可提供手机号登录、资料刷新、昵称修改、退出、新老用户分流、Frozen/Unknown 受限分流、修改结果未知恢复，以及登录/资料操作状态。`UserUiBinder` 和无 UI 对象装配已经在逻辑分支完成，页面只需通过 `IUserUiBinder` 发送意图、接收 ViewState 和导航请求。

最新服务端协议 v2.4 暂未提供普通用户资料查询和昵称修改专用消息。小 Demo 必须使用 `MockUserNetworkApi` 跑完整用户流程；不要在 UI 中调用 `GETDATA(100)`、`UPDDATA(111)` 或自行拼协议绕过。真实网络联调当前只适合验证手机号登录等协议已有能力。

## P0：小 Demo 最短路径（当前最急）

- [ ] 先只迁移/改造登录页与资料编辑页，不等待所有窗口完成目录重构。
- [ ] 登录页提供 `loginRequested(const QString &phone)` 和 `render(const LoginViewState &state)`；删除点击后直接 `loginSucceeded()`、`hide()` 或硬编码新老用户跳转。
- [ ] 资料页提供 `profileSaveRequested(const QString &nickname)` 和 `render(const ProfileEditViewState &state)`；删除点击后直接 `profileSaved()`、`hide()` 或直接进入首页。
- [ ] 提供一个独立 Demo 装配入口，以 `MockUserNetworkApi + UserApplicationAssembly + IUserUiBinder` 接线：页面意图连接 Binder 槽，Binder 的 ViewState 信号连接页面 `render()`，`navigationRequested` 只负责切换页面。
- [ ] Demo 至少可操作跑通三条链路：新用户登录 → 完善昵称 → 首页；老用户登录 → 首页；网络失败 → 输入保留 → 重试成功。
- [ ] Demo 额外提供可快速切换的 Loading、ValidationError、ServerError、ResultUnknown、Frozen/Unknown 展示状态；这些演示配置放在独立 Demo/测试夹具中，不写入正式 Widget。
- [ ] 页面完成后，把信号、`render()` 声明和所需 `objectName` 变更单独提交，通知逻辑负责人接入；不要直接复制逻辑分支的业务源码。
- [ ] 在 BitDev / Ubuntu 22.04 / Qt 5.15.3 中完成构建和三条链路的交互验收，记录命令、结果、截图及未验证项。

小 Demo 完成判定：`src/main.cpp` 中不再依赖 `demoIsNewUser` 或按钮即成功的假分流；所有成功、失败和页面去向都来自 Mock 驱动的 Binder 状态/导航信号。独立 Demo 可以有自己的入口，但不能成为正式 `main.cpp` 的默认硬编码路径。

## P1：冻结并完善页面接缝

- [ ] 建立 `src/presentation/contracts/`、`src/presentation/pages/`、`src/presentation/widgets/` 和 `src/presentation/presentation.pri`；根目录 `ui/` 继续只存 Qt Designer 文件。
- [ ] 迁移时保留现有窗口类名，逐页迁移 `src/*window.h/.cpp` 至 `src/presentation/pages/`；不要批量移动无关页面。
- [ ] 与逻辑负责人冻结两个语义化意图信号：

  ```cpp
  void loginRequested(const QString &phone);
  void profileSaveRequested(const QString &nickname);
  ```

- [ ] 页面必须提供只负责展示的 `render(...)` 方法。ViewState 至少能表达输入草稿、提交中、可提交、展示文案、可重试、结果未知和账号受限；不要把 `UserService`、`AppFlowCoordinator`、错误码、Socket 或协议类型暴露给页面。
- [ ] 在 UI 分支记录最终信号签名、ViewState 字段和 `objectName`，提交给逻辑负责人后再冻结。

## P1：登录页完整状态

- [ ] 将手机号输入框 `objectName` 固定为 `editPhoneNumber`，登录按钮固定为 `btnLogin`。
- [ ] 点击登录只发出 `loginRequested(ui->editPhoneNumber->text())`；删除按钮点击后直接 `loginSucceeded()`、`hide()` 或自行跳转页面的逻辑。
- [ ] 增加 `errorLabel` 和 `loadingIndicator`（可用 QLabel/QMovie/占位控件实现），并由 `render(...)` 控制可见性。
- [ ] Loading 时禁用 `btnLogin`；失败后保留用户输入；重试入口重新发出当前手机号的登录意图。
- [ ] UI 可以做输入提示，但手机号是否合法、是否允许重复提交由逻辑层决定。
- [ ] 覆盖空输入、11 位输入、加载、网络/服务端错误、结果未知和 Frozen/Unknown 限制提示的视觉状态。

## P1：资料完善/昵称编辑页完整状态

- [ ] 将昵称保存动作改为 `profileSaveRequested(nickname)`；删除按钮点击即 `profileSaved()`、`hide()` 或自行进入首页的逻辑。
- [ ] 手机号字段只读；昵称输入控件保留/冻结明确的 `objectName`，并在交接文档中说明。
- [ ] 增加 `errorLabel`、`loadingIndicator` 和结果未知持续提示；失败或刷新时不能清空用户正在编辑的昵称草稿。
- [ ] Loading 时禁用保存按钮；普通失败显示可重试入口；结果未知显示“正在确认服务端状态”，不能诱导用户连续点击保存。
- [ ] 新用户默认昵称可能为“用户 + 手机号后四位”；页面应能展示该草稿，但不自行调用保存或判断是否新用户。
- [ ] Frozen/Unknown 状态必须有持续可见提示；页面不自行判断受限规则。

## P2：登录后容器与“我的”页骨架

- [ ] 将登录后页面收敛为 `MainWindow + QStackedWidget`，固定一级入口：首页、充电、我的；二级页在同一容器内切换，不再创建新的顶层窗口。
- [ ] `MainWindow` 只暴露页面显示/返回等展示性槽或信号；新用户去资料页、老用户去首页、受限用户去受限首页由后续 Binder 根据逻辑导航意图决定。
- [ ] 补齐“我的”页面展示骨架：头像占位、昵称、脱敏手机号、账号状态、编辑资料入口、充值入口、退出入口。
- [ ] “我的”页对外只发语义化意图，例如 `profileEditRequested()`、`rechargePageRequested()`、`logoutRequested()`；不直接读取余额、发网络请求或注销会话。
- [ ] Frozen/Unknown 状态展示受限提示，并禁用由产品规则明确禁止的入口；最终权限仍以服务端为准。

## P2：完整演示与验收

- [ ] 建立 UI 演示入口或测试夹具，用固定 ViewState 验证页面；演示数据不能写入正式 Widget，也不能成为正式 `main.cpp` 默认路径。
- [ ] 验收长昵称、长错误文案、小窗口、资源缺失占位、重复点击、输入保留、Loading、结果未知和受限状态。
- [ ] 在 BitDev（Ubuntu 22.04、Qt 5.15.3、qmake）构建 UI，记录命令、结果和未验证项。

## 禁止事项

- 不在 Widget 中调用 `IUserService`、`AppFlowCoordinator`、`BackendClient`、`QTcpSocket` 或 `MassageHandler`。
- 不在页面拼 JSON、引用消息码、访问数据库，或用定时器伪造业务成功。
- 不根据按钮点击推断新老用户、Frozen、修改成功或导航目的地。
- 不改动 `src/modules/`、`src/network/`、`src/flow/`、`protocol.h`、`massagehandler.*`。
- 未与逻辑负责人确认前，不改 `ChargingUser.pro`、`main.cpp`、公共信号、资源别名和已冻结的 `objectName`。

## 建议提交拆分

1. `refactor(ui): add presentation structure and contracts`
2. `feat(ui): connect login and profile demo intents`
3. `test(ui): add mock user flow demo`
4. `feat(ui): complete login and profile render states`
5. `feat(ui): add stacked main container and profile page skeleton`
6. `test(ui): add target UI validation notes`
