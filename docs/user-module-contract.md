# M1 用户身份与资料模块接口及并行开发约定

## 1. 文档状态

- 状态：阶段 1 已确认，可作为逻辑开发和 UI 设计的共同输入。
- 基线：`user-design-main` 的提交 `2cb25c3`。
- 逻辑开发分支：`user-module-main`。
- UI 设计分支：`user-design-main`。
- 目标环境：Ubuntu 22.04、Qt 5.15.3、qmake、C++17。
- 上位设计：`docs/02概要设计说明书第8组--客户端综合设计.docx`。

本文细化综合设计中的 M1 用户身份与资料模块，冻结领域对象、异步接口语义、UI 事件和下一步代码清单。钱包与订单属于 M3，页面流转和结果未知恢复属于 M4。具体网络消息编号仍以服务端最终协议为准，客户端不得自行占用未分配的消息码。

## 2. 首版功能范围

### 2.1 必须实现

1. 11 位手机号免密登录，用户不存在时由服务端原子自动注册。
2. 登录请求防重复提交，以及非法输入、超时、断网和服务端错误处理。
3. 在内存中维护当前用户身份、资料、账号状态和重新认证所需信息。
4. 新用户进入资料完善页，老用户进入首页。
5. 查询用户资料并修改昵称；手机号只读。
6. Frozen 用户可以建立会话，但 M1 必须向 M2/M3/M4公开明确账号状态。
7. 退出登录，清理会话、用户级任务和迟到响应。
8. 网络恢复后重新登录，再从服务端刷新用户身份和资料。

### 2.2 首版不实现

- 短信验证码、密码登录和修改手机号。
- 本地或远程头像上传；首版只展示服务端预置头像资源标识。
- 客户端直连 SQLite 或调用高权限通用增删改查接口。
- Token 持久化和自动登录；服务端未提供 Token 合同时按连接会话处理。
- 余额查询、充值、钱包流水、订单和结算；这些功能由 M3 负责。
- 页面容器、底部导航和跨模块跳转；这些功能由 M4 负责。

## 3. 已确认的业务与接口决定

| 事项 | 首版决定 |
|---|---|
| 新用户标识 | `autoRegistered` 映射为客户端 `isNewUser` |
| 用户唯一标识 | 服务端 `username` 作为首版 `userId` |
| 请求关联 | 每个异步操作携带并回传 `requestId`；客户端使用 UUID 字符串 |
| 变更幂等 | 修改资料等变更操作另带 `operationId`；结果未知时查询状态，不直接重做 |
| 并发规则 | 同一类用户操作同一时间只允许一个进行中请求 |
| 用户资料修改 | 使用普通用户专用接口，禁止调用高权限 `UPDDATA(111)` |
| 资料刷新 | 使用普通用户专用查询接口；登录和重连后均执行 |
| 退出登录 | 服务端支持时等待退出应答；未支持时本地清理并关闭当前用户上下文 |
| 默认昵称 | 服务端生成“用户 + 手机号后四位” |
| Frozen 登录 | 允许登录并进入受限首页，操作权限仍由服务端最终校验 |
| 头像 | 服务端返回预置头像资源标识 `avatarKey`，客户端从 `.qrc` 映射显示 |
| 会话保存 | 首版仅保存在内存；未冻结 Token 合同前不写磁盘 |
| 页面流转 | M1 只发布业务结果；新老用户分流、主页面切换和会话失效跳转由 M4 处理 |

## 4. 领域数据合同

建议在 `src/common/` 和 `src/modules/user/` 中定义不依赖具体 Widget 的值类型。

```cpp
enum class AccountStatus {
    Normal,
    Frozen,
    Unknown
};

struct UserProfile {
    QString userId;
    QString phone;
    QString nickname;
    QString avatarKey;
};

struct UserSession {
    UserProfile profile;
    AccountStatus accountStatus = AccountStatus::Unknown;
    bool authenticated = false;
};

struct RequestContext {
    QString requestId;
    QString operationId;
};

struct LoginResult {
    QString requestId;
    UserSession session;
    bool isNewUser = false;
    bool profileCompleted = false;
};

struct ClientError {
    QString requestId;
    QString operationId;
    QString code;
    QString displayMessage;
    bool retryable = false;
    bool resultUnknown = false;
};
```

约束：

- `Unknown` 账号状态按受限状态处理，直到服务端给出明确结果。
- 查询操作的 `operationId` 为空；资料修改等变更操作必须同时携带 `requestId` 和 `operationId`。
- `resultUnknown == true` 时不得直接重复提交，应把操作交给 M4 的 `IPendingOperationStore`。
- 日志中的手机号必须脱敏，不记录完整响应、Token 或其他会话凭据。
- 跨线程传递自定义值类型时使用 `Q_DECLARE_METATYPE` 并完成注册。

## 5. M1 业务与网络接口

页面依赖 `IUserService`，`UserService` 依赖 `IUserNetworkApi`。UI 不接触协议编号、JSON 或 `BackendClient`。M1 网络接口至少包含：

```cpp
void loginByPhone(const QString &phone, const RequestContext &context);
void queryCurrentUser(const QString &userId, const RequestContext &context);
void updateNickname(const QString &userId,
                    const QString &nickname,
                    const RequestContext &context);
void logout(const RequestContext &context);
```

每个请求必须产生且只产生一次终态：确认成功、确认失败或结果未知。查询超时通常为可重试失败；资料修改等变更操作超时必须携带 `operationId` 进入结果未知恢复流程。迟到响应按 `requestId` 和当前会话世代共同过滤。

### 5.1 已交付的网络传输实现

网络同学已提供：

- `protocol.h`：帧常量、消息类型、业务状态和错误码。
- `massagehandler.h/.cpp`：无 socket 依赖的共享编解码器。
- `协议与接口说明.md`：v2 字段、状态机和服务端分层说明。

实际接收链路为：

```text
QTcpSocket::readyRead
    -> socket->readAll()
    -> MassageHandler::feed(bytes)
    -> 粘包/半包解析与 400/401/402 分片重组
    -> frameReady(msgType, payload)
    -> BackendClient 内的 IUserNetworkApi 适配器校验 JSON 和业务字段
    -> IUserNetworkApi 终态信号
```

实际发送链路为：

```text
领域请求
    -> BackendClient 内的 IUserNetworkApi 适配器构造 QJsonObject
    -> MassageHandler::pack(msgType, json)
    -> INetworkTransport 实现写入 QTcpSocket
```

传输约束：

- 帧头使用网络大端序：`4B msgType + 8B msgSize`。
- JSON 为 UTF-8 Compact 文本；心跳 107 是真正的空载荷帧，普通空对象会编码为 `{}`。
- 单物理帧载荷最多 8MB；业务载荷超过 16KB 后自动分片，上层收到重组后的原始业务类型。
- `MassageHandler` 是有状态解析器，不可由多个线程同时调用；它与对应 socket 必须属于同一工作线程。
- 断线、切换 socket 或开始新连接前必须调用 `reset()`，防止旧半包污染新连接。
- `fromPayload()` 对非法 JSON 和合法空对象都会返回空对象，领域网络适配器必须另外校验消息类型、必填字段和字段类型，不能以“得到 QJsonObject”作为成功条件。
- 当前分片实现只限制每个物理帧大小，尚未限制一次重组的累计大小、分片数量和等待时间；修改共享源码前需与网络负责人同步，正式联调前应补齐防护。

`MassageHandler` 不包含 `QTcpSocket`、发送队列、连接状态、心跳计时、业务请求超时、请求关联或自动重连。这些能力由网络负责人通过 `INetworkTransport`、`INetworkObserver` 和 `BackendClient` 实现；M1 不另建连接管理器。工程接入时需在 `.pro` 中增加 Qt Network 模块。

### 5.2 已有用户协议映射

| 领域操作 | 当前消息 | 客户端映射 |
|---|---|---|
| 手机号登录 | `116 -> 217` | `username -> userId`、`status -> accountStatus`、`autoRegistered -> isNewUser`；随登录返回的 `balanceCents` 转交 M3，不存入 M1 会话 |
| 业务错误 | `300~305` | 优先映射 `code`，`err` 仅作为未知错误兜底且不直接长期绑定 UI 文案 |

手机号登录目前没有对应的 `MassageHandler` 构造辅助函数，网络负责人应在 `IUserNetworkApi` 适配器内部构造 JSON 后调用 `pack()`，不要为领域字段修改共享编解码器。充值 `113 -> 216` 属于 M3 的 `IWalletNetworkApi`，不在 M1 中实现。

### 5.3 当前协议与用户合同的差距

当前 v2 载荷没有 `requestId`、`operationId`，错误响应也没有原请求类型。若允许并发请求，客户端无法判断一个 3xx 错误属于哪个业务，也无法在资料修改等变更操作超时后查询最终结果。服务端需为请求和应答回显 `requestId`，为变更操作接受 `operationId`，并在错误响应中增加原请求类型或等价关联字段。

服务端还需分配正式消息码并补齐：

1. 查询当前用户身份与资料。
2. 修改当前用户昵称。
3. 退出登录应答。
4. 登录结果中的 `profileCompleted` 和 `avatarKey`。
5. 默认昵称由当前完整手机号改为已确认的“用户 + 手机号后四位”。
6. 按 `operationId` 查询资料修改等结果未知操作的结果，或提供跨模块统一查询接口。

M3 还需单独确认钱包查询、充值 `operationId`、整数分和充值后总余额语义。在这些接口冻结前，`MockUserNetworkApi` 可以实现 M1 完整语义；真实适配器不得借用 `GETDATA(100)` 或高权限 `UPDDATA(111)` 猜测实现。

## 6. UI、装配与逻辑层并行合同

三层必须物理分离：

```text
presentation（UI 负责人） -> app（公共装配接缝） -> modules/flow（逻辑负责人）
                                                   -> network（网络负责人）
```

- `presentation` 管理 Widget、输入读取、视觉渲染和所有 UI 响应，不包含业务判断。
- `app` 连接页面意图、业务服务与流程协调器，并把业务结果映射为稳定的页面状态；不直接操作具体控件。
- `modules/flow` 管理业务规则、会话和流程，不包含 `QWidget`、`Ui::*` 或 `objectName`。

UI 同学可以用演示装配器和 Mock 独立运行页面；正式 Widget 不直接持有 `IUserService` 或网络对象。

### 6.1 UI 发出的用户意图

```cpp
// LoginPage
void loginRequested(const QString &phone);

// ProfileEditPage
void profileSaveRequested(const QString &nickname);

// ProfilePage（组合页面，事件分别交给 M1/M3/M4）
void profileEditRequested();
void rechargePageRequested();
void logoutRequested();
```

Widget 不得自行判定新老用户、修改余额、建立会话或直接跳转业务流程。登录、资料和退出交给 M1 的 `IUserService`，充值交给 M3 的 `IWalletService`，页面跳转由 M4 的 `IAppFlowCoordinator` 根据已确认结果决定。

### 6.2 逻辑层提供给 UI 的状态

所有提交型页面使用一次性状态快照渲染，至少支持：`Idle`、`Loading`、`Success`、`ValidationError`、`NetworkError`、`ServerError`、`ResultUnknown`。用户中心额外展示 `Normal`、`Frozen` 和 `Unknown`。UI 负责人实现 `render(...)`，装配层产生状态，业务层不调用 `setText()`、`setEnabled()` 或弹窗。

首批共享 UI 状态建议冻结为：

```cpp
enum class SubmitState {
    Idle,
    Loading,
    Success,
    ValidationError,
    NetworkError,
    ServerError,
    ResultUnknown
};

struct LoginViewState {
    SubmitState submitState = SubmitState::Idle;
    QString phoneInput;
    QString message;
    bool canSubmit = true;
};

struct ProfileEditViewState {
    SubmitState submitState = SubmitState::Idle;
    QString phone;
    QString nicknameInput;
    QString message;
    bool canSubmit = true;
};
```

这些类型只表达页面需要显示的内容，不保存 Widget 指针，也不包含协议码、JSON、Service 或流程判断。具体动画、颜色、文案摆放和控件启停由 UI 实现决定。

加载期间：

- 保留用户已输入内容。
- 禁用当前提交按钮，其他是否可用由页面交互规范决定。
- 不重复发送同类请求。
- 失败后恢复按钮并显示可操作提示。

### 6.3 页面和 objectName 接缝

综合设计冻结以下登录页 objectName，现有 UI 中的旧名称由 UI 分支统一迁移：

- `phoneEdit` -> `editPhoneNumber`。
- `loginButton` -> `btnLogin`。

登录后的主界面使用 `MainWindow + QStackedWidget`，首页、充电、我的构成固定底部一级导航。站点详情、地图导航、充电确认、结算、资料、钱包和订单详情作为二级页面，不继续扩展为彼此独立的顶层窗口。

UI 负责人需要补充并在合并前确认：

- 登录页：`errorLabel`、`loadingIndicator`。
- 资料页：`errorLabel`、`loadingIndicator`。
- “我的”页面：`profileCard`、`walletCard`、`btnRecharge`、`profileMenuList`、`btnLogout`。
- 充值交互：金额输入、快捷金额、提交、取消、加载和错误控件。
- Frozen 状态：持续可见的限制提示，不仅使用一次性弹窗。

UI 可以调整布局和视觉样式，但不得单方面重命名已冻结的 objectName、窗口公共信号或 DTO 字段。

## 7. 分支和文件所有权

| 范围 | 主要分支/负责人 | 说明 |
|---|---|---|
| `src/presentation/`、`.ui`、`.qss`、图片和用户中心页面 | `user-design-main` / UI | Widget、视觉、布局、输入、渲染、反馈状态和 objectName |
| `src/presentation/contracts/` | 共同冻结、UI 主维护 | 页面意图和 ViewState；不得包含业务实现或控件指针 |
| `src/app/` | 公共装配接缝 | 依赖注入、信号连接、业务结果到 ViewState 的映射 |
| `src/common/`、`src/modules/user/`、M1 测试 | `user-module-main` / 逻辑 | 共享 DTO、`IUserService`、用户状态、Mock 和测试 |
| `src/flow/` | M4 / 共同确认 | 页面流转、跨模块用例、待处理操作恢复 |
| `BackendClient`、`INetworkTransport`、领域网络适配器 | 网络负责人 | 连接、序列化、协议映射、超时、重连和异步结果 |
| `protocol.h`、`massagehandler.h/.cpp`、`协议与接口说明.md` | 网络负责人 / 共享输入 | 帧、消息码和字段合同；修改必须同步两端 |
| `ChargingUser.pro`、`src/main.cpp` | 共同接缝 | 小范围提交，修改前对照本文合同 |
| `resources.qrc` | UI 为主、共同确认 | 逻辑只依赖冻结后的资源别名 |

合并时禁止整文件覆盖另一分支的 `.ui`、`.qss` 或 `main.cpp`。公共接缝变更应拆成独立提交，便于评审和解决冲突。

## 8. 目标项目结构与依赖

```text
ChargingUser/
├── ChargingUser.pro
├── protocol.h
├── massagehandler.h/.cpp
├── 协议与接口说明.md
├── src/
│   ├── main.cpp
│   ├── app/
│   │   ├── application.h/.cpp       # 正式对象装配
│   │   ├── useruibinder.h/.cpp      # UI 意图/状态与 M1 的连接
│   │   └── app.pri
│   ├── common/
│   │   ├── requestcontext.h
│   │   ├── clienterror.h
│   │   ├── operationresult.h
│   │   └── connectionstate.h
│   ├── network/
│   │   ├── inetworktransport.h
│   │   ├── inetworkobserver.h
│   │   ├── backendclient.h/.cpp
│   │   └── network.pri
│   ├── modules/
│   │   ├── user/          # M1：身份、资料、会话
│   │   ├── charger/       # M2：站点与 Charger
│   │   ├── charging/      # M2：充电控制
│   │   ├── order/         # M3：订单与结算
│   │   └── wallet/        # M3：余额与充值
│   ├── flow/                        # M4：页面流转和跨模块恢复
│   └── presentation/                # UI 分支独立维护
│       ├── contracts/               # 稳定 ViewState/页面意图接缝
│       ├── pages/                   # 页面 Widget 的 .h/.cpp
│       ├── widgets/                 # 可复用展示组件
│       └── presentation.pri
├── ui/                              # Qt Designer 文件，UI 负责人维护
├── styles/                          # QSS，UI 负责人维护
├── resources/                       # UI 资源，别名属于共享接缝
└── tests/
    ├── protocol/
    ├── user/
    └── flow/
```

现有窗口源码暂时保留原路径，避免与 UI 分支发生整文件冲突。迁移由 UI 分支按页面逐个完成，并同步更新 `presentation.pri`；逻辑分支不批量移动窗口文件。`protocol.h`、`massagehandler.h/.cpp` 也暂留根目录，未经网络负责人确认不移动。

依赖方向固定为：

```text
Page --intent signal--> UserUiBinder -> IUserService -> UserService -> IUserNetworkApi
Page <--render(ViewState)-- UserUiBinder
IUserNetworkApi 真实实现 -> BackendClient -> INetworkTransport -> QTcpSocket/MassageHandler
UserService 业务结果 -> IAppFlowCoordinator -> UserUiBinder -> MainWindow/QStackedWidget
```

禁止反向依赖：M1 不包含 Widget、ViewState 或协议宏，网络层不控制页面，M4 不解析 JSON，M1 不调用 M3 的钱包实现，Page 不直接调用 Service，`UserUiBinder` 不实现业务规则。

## 9. 下一步需要新增或更新的代码

按以下顺序逐步实施，不在一个提交中同时完成全部内容。

### 步骤 1：修正规划与构建骨架

规划基线：

- 综合设计作为 M1—M4 上位架构。
- 本文作为 M1 细化合同。
- 钱包和充值移至 M3，页面流转和结果未知恢复移至 M4。

构建更新：

- `ChargingUser.pro`：增加 `network` 模块，登记 `protocol.h`、`massagehandler.h/.cpp`，并补齐 `DESTDIR`、`MOC_DIR`、`UI_DIR`、`RCC_DIR`、`OBJECTS_DIR`。
- 新增模块 `.pri` 文件，让 M1、M4 和网络负责人分别维护自己的源文件清单，减少共同修改主 `.pro`。
- 暂不增加 `webenginewidgets`；M2 接入腾讯地图前先确认 Ubuntu Kit 已安装 Qt WebEngine。

### 步骤 2：共享合同与 M1 接口骨架

新增：

- `src/common/requestcontext.h`
- `src/common/clienterror.h`
- `src/common/operationresult.h`
- `src/common/connectionstate.h`
- `src/modules/user/usertypes.h`
- `src/modules/user/iuserservice.h`
- `src/modules/user/iusernetworkapi.h`
- `src/modules/user/user.pri`

这一批不依赖 Widget、JSON、`QTcpSocket` 或具体消息码。

并行 UI 工作由 `docs/ui-collaboration-guide.md` 约束。UI 分支可先新增 `src/presentation/contracts/`、`pages/`、`widgets/` 和 `presentation.pri`，实现页面意图信号及 `render(ViewState)`，但不修改 M1 业务接口实现。

### 步骤 3：M1 Mock、服务与自动化测试

新增：

- `src/modules/user/mockusernetworkapi.h/.cpp`
- `src/modules/user/userservice.h/.cpp`
- `tests/user/user-module-tests.pro`
- M1 正常、新用户、Frozen、失败、超时、结果未知和迟到响应测试数据。

要求：Mock 必须支持可控结果和延迟，不把固定演示值写入正式 Widget。

### 步骤 4：M4 页面流转骨架

新增：

- `src/flow/iappflowcoordinator.h`
- `src/flow/appflowcoordinator.h/.cpp`
- `src/flow/ipendingoperationstore.h`
- `src/flow/flow.pri`

M4 消费 M1 的登录、退出和会话失效结果，负责新用户进入资料页、老用户进入首页、Frozen 进入受限首页以及返回登录页。M1 不持有窗口指针。

### 步骤 5：装配层接入语义化 UI 接口

更新：

- UI 分支把登录页 objectName 迁移为 `editPhoneNumber`、`btnLogin`，并提供加载和错误状态。
- UI 分支将 `src/loginwindow.h/.cpp` 迁入 `src/presentation/pages/`，把无参数 `loginSucceeded()` 改为 `loginRequested(phone)`，并实现 `render(LoginViewState)`。
- UI 分支将 `src/profileeditwindow.h/.cpp` 迁入 `src/presentation/pages/`，把无参数 `profileSaved()` 改为 `profileSaveRequested(nickname)`，手机号只读，并实现 `render(ProfileEditViewState)`。
- 逻辑分支新增 `src/app/useruibinder.h/.cpp`，只负责连接页面意图、M1 结果和 M4 流程。
- `src/main.cpp`：删除 `demoIsNewUser`，只创建正式应用装配对象并启动；不直接连接具体业务分支。
- 登录后页面逐步收敛为 `MainWindow + QStackedWidget`；具体页面改造由 UI 分支执行。

此步骤应在 UI 分支确认公共信号、ViewState 和 objectName 后进行。UI 响应实现与业务接入分为独立提交，便于双方合并。

### 步骤 6：“我的”页面跨模块集成

UI 分支新增窗口后，逻辑分支接入：

- M1 提供头像、昵称、脱敏手机号、账号状态、修改资料和退出。
- M3 提供钱包余额、充值和订单摘要。
- M4 负责编辑资料、充值、订单和退出入口的页面跳转。
- Frozen 状态由 M1 发布，M2/M3 执行自身操作限制，服务端继续进行最终校验。

### 步骤 7：真实网络适配

网络负责人确认第 5.3 节协议差距后提供：

- `INetworkTransport` 与 `INetworkObserver` 实现。
- `BackendClient`。
- `IUserNetworkApi` 真实适配器。
- 请求/响应关联、超时、错误映射、迟到响应丢弃和重连恢复。

M1 不重复实现 socket、心跳或重连，不让 `BackendClient` 直接控制页面。服务端请求关联、`operationId` 恢复和普通用户资料接口未冻结前不开始真实联调。

### 步骤 8：测试与目标环境验证

- 非法手机号不发送请求。
- 重复登录和保存只产生一个进行中请求。
- 新用户、老用户和 Frozen 用户分流正确。
- 资料保存失败不跳页、不丢输入。
- 退出后迟到响应不恢复旧会话。
- 重连后重新登录并刷新服务端权威身份与资料。
- 资料修改结果未知时按 `operationId` 查询，不直接重做。
- 半包、粘包和分片重组后只交付一条完整业务消息。
- 断线重连前清空旧解析状态，旧连接的迟到响应不进入新会话。
- 心跳、业务超时和重连定时器不会阻塞 UI 线程。
- 非法 JSON、缺失字段、错误字段类型和未知消息码不会污染用户会话。
- Ubuntu/Qt 5.15.3 构建与窗口行为验证。

## 10. 阶段 1 完成条件

- 综合设计和本文的 M1—M4 边界已经由逻辑、UI 和网络负责人确认。
- 服务端收到第 5.3 节接口需求并确认消息合同负责人。
- UI 负责人确认第 6 节页面事件、状态和 objectName。
- 下一步只开始“步骤 1：修正规划与构建骨架”，不提前把未冻结消息码写入代码。

## 11. 合同变更记录 v1.1 —— 最小协议扩展（2026-09-04）

> 变更动机：实训项目交付时间紧（9 月 9 日）。经组内授权由客户端侧单方面确认本扩展，
> 目标是功能可实现、安全边界不破坏、服务端改动最小（预估 ≤80 行）。
> 本节与本文件前文冲突处，以本节为准；`协议与接口说明.md` 已同步升级为 v2.1。

### 11.1 新增消息码（服务端唯一新增开发量）

| 类型码 | 宏 | 方向 | 说明 |
|---|---|---|---|
| 117 | QUERY_PROFILE_REQ | 用户→S | 查询当前用户身份与资料 |
| 218 | PROFILE_ACK | S→用户 | 资料应答 |
| 118 | UPDNICK_REQ | 用户→S | 修改当前用户昵称 |
| 219 | UPDNICK_ACK | S→用户 | 修改结果（回显 nickname） |

```json
// 117 请求
{ "username": "U13800000000", "requestId": "uuid" }
// 218 应答
{ "username": "U13800000000", "phone": "13800000000", "nickname": "用户0000",
  "status": "Normal", "balanceCents": 0, "requestId": "uuid" }
// 118 请求
{ "username": "U13800000000", "nickname": "用户0000", "requestId": "uuid" }
// 219 应答
{ "ok": true, "nickname": "用户0000", "requestId": "uuid" }
```

安全要求（本节唯一强制项）：

- 服务端处理 117/118 前必须校验当前连接已通过 116 登录为该 `username`，
  禁止查询或修改他人资料；未登录或身份不符回 302/NOT_LOGIN。
- 服务端应限制 nickname 长度（建议 ≤20 字符）并过滤控制字符。

### 11.2 条款修订（原 5.3 节需求清单的降级与替代）

| 原需求 | v1.1 处理 | 说明 |
|---|---|---|
| R1 requestId 回显 | 客户端继续携带；服务端**能回显则回显**（每个应答拷贝一个字段），不回显也可接受 | 不回显时 M1 按“消息类型 + 会话世代”关联。M1 同类请求防重保证无歧义；M2/M3 出现并发在途场景时再补回显 |
| R2 3xx 错误关联 | 服务端不改 3xx | 3xx 归属“该连接最近在途请求”；同上，M1 前提下无歧义 |
| R3/R4 资料查询与改昵称消息码 | **由 117/118 替代，服务端必须实现** | 复用 user 表现有查询/更新，无新表 |
| R5 operationId 结果查询 | **取消服务端 operationId** | 改昵称是幂等操作：超时按结果未知处理 → 由 M4 `IPendingOperationStore` 发起 117 查询比对，未生效才重新提交。“不得直接重复提交”条款不变 |
| R6 退出应答 | 服务端不新增 2xx | 102 尽力发送、不等待应答，客户端本地清理（§3 已允许）；断连时服务端自然清理 |
| R7 217 补字段 | 217 载荷不变 | `profileCompleted` 由客户端推导（`autoRegistered && 本次会话无资料保存成功`）；首版头像使用客户端内置默认资源，`avatarKey` 延后 |
| R8 默认昵称规则 | 服务端注册逻辑不改 | 自动注册成功后，客户端立即经 118 提交“用户+手机号后四位”；失败则暂显完整手机号，后续可重试 |

原 §7 步骤 7“未冻结前不开始真实联调”修订为：**本节即冻结记录，服务端 117/118 就绪后即可开始真实联调**。

### 11.3 不变的红线

- 仍然禁止借用 `GETDATA(100)`/`DATA(200)` 通用通道与 `110/111/112` 高权限通道实现本功能。
- `protocol.h`/`massagehandler.h/.cpp` 由客户端侧按本次授权增补常量，同步更新
  `协议与接口说明.md`（v2.1），并知会服务端负责人；不得改动帧格式、分片与心跳语义。
- 日志脱敏、单端终态、M1—M4 依赖方向、Widget/业务分离等条款全部不变。
