# ChargingUser UI 并行开发指南

> 目录与职责补充：以 `docs/project-structure.md` 为最新约定。根目录 `ui/` 保留 Designer 文件；页面 C++ 响应放入 `src/presentation/`，两部分均由 UI 负责人维护。目标结构与当前已实现代码应分别理解。

## 1. 目标与工作范围

UI 分支 `user-design-main` 独立负责页面 Widget、Qt Designer 文件、样式、资源以及加载、空态、错误、禁用、重试等视觉响应。业务分支 `user-module-main` 不操作具体控件，也不要求 UI 代码了解协议、网络连接或服务端消息码。

共同基线为 `2cb25c3`。完整业务合同见 `docs/user-module-contract.md`，本文只说明 UI 同学可以直接开展的工作和双方接缝。

## 2. 目录所有权

```text
src/presentation/
├── contracts/          # 页面 ViewState；双方冻结后 UI 主维护
├── pages/              # 页面 QWidget 的 .h/.cpp
├── widgets/            # 可复用纯展示组件
└── presentation.pri    # UI 源文件登记

ui/                     # Qt Designer 文件
styles/                 # QSS
resources/              # 图片、图标、字体和 qrc
```

UI 分支不修改：

- `src/modules/` 中的 Service、领域状态和业务规则。
- `src/network/`、`protocol.h`、`massagehandler.*`。
- `src/flow/` 中的新老用户分流、会话失效和结果未知恢复逻辑。
- `src/app/` 中的正式业务装配；需要接入时只提交接口需求。

## 3. 页面编程接口

页面向外只发出用户意图：

```cpp
// LoginPage
void loginRequested(const QString &phone);

// ProfileEditPage
void profileSaveRequested(const QString &nickname);

// ProfilePage
void profileEditRequested();
void rechargePageRequested();
void logoutRequested();
```

页面通过单一状态快照响应外部结果：

```cpp
void LoginPage::render(const LoginViewState &state);
void ProfileEditPage::render(const ProfileEditViewState &state);
```

`render()` 内可以更新文字、控件启停、加载动画、焦点和可访问性提示，但不得建立会话、判断新老用户、计算余额、决定业务跳转或发起网络请求。

## 4. 首批 ViewState

双方首先冻结 `SubmitState`、`LoginViewState` 和 `ProfileEditViewState`。建议字段见 `docs/user-module-contract.md` 第 6.2 节。

约束：

- `message` 是可直接展示的用户文案，UI 不解析错误码。
- `phoneInput`、`nicknameInput` 用来在失败和重绘后保留输入。
- `canSubmit` 控制提交入口；UI 不自行推断网络或业务是否允许提交。
- UI 可以根据 `submitState` 选择行内错误、Toast 或动画，但结果未知必须给出持续可见提示，不能伪装为普通失败。
- Frozen 和 Unknown 必须使用持续可见的账号限制提示，不能只弹一次对话框。

## 5. 首批页面任务

1. 建立 `src/presentation/contracts/`、`pages/`、`widgets/` 和 `presentation.pri`。
2. 将登录页迁入 `pages/`，把 `loginSucceeded()` 改为 `loginRequested(phone)`。
3. 登录页 objectName 使用 `editPhoneNumber`、`btnLogin`，补充 `errorLabel`、`loadingIndicator`。
4. 将资料编辑页迁入 `pages/`，把 `profileSaved()` 改为 `profileSaveRequested(nickname)`；手机号只读。
5. 资料页补充 `errorLabel`、`loadingIndicator`，覆盖 Idle、Loading、校验错误、网络错误、服务端错误和结果未知状态。
6. 登录后的一级页面收敛到 `MainWindow + QStackedWidget`，底部固定为首页、充电、我的；二级页面不再新增独立顶层窗口。
7. “我的”页面预留 `profileCard`、`walletCard`、`btnRecharge`、`profileMenuList`、`btnLogout`。

## 6. 独立演示与 Mock

UI 可以建立仅供演示的装配入口，依次向页面传入不同 ViewState，检查加载、错误、Frozen、长昵称和小窗口布局。演示代码不得写进正式 Widget，不得伪造业务成功信号，也不得成为正式 `main.cpp` 的默认路径。

建议至少覆盖：

- 空手机号、非法手机号和 11 位合法手机号。
- Loading 时重复点击提交。
- 网络错误后保留原输入并允许重试。
- 很长的昵称和错误文案。
- Frozen、Unknown 和结果未知的持续提示。
- 图片资源缺失时的占位显示。

## 7. 合并接缝

以下变更必须先同步逻辑负责人：

- 页面意图信号签名或 ViewState 字段变化。
- 已冻结 `objectName` 或资源别名变化。
- `ChargingUser.pro`、`src/main.cpp`、`src/app/` 的修改。
- 页面流转规则和新老用户/Frozen 行为变化。

提交应按“目录骨架与合同”“单个页面迁移”“视觉与资源”“状态覆盖”拆分，避免整批移动和业务接入混在同一个提交。合并前在 Ubuntu 22.04、Qt 5.15.3 目标环境验证构建、资源加载和页面交互。
