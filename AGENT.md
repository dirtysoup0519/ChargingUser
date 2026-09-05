# ChargingUser 项目工作约定

目录重构与 UI 交接的最新规范见 `docs/project-structure.md`（2026-09-05），其中 TODO 由 UI 负责人完成。根目录 `ui/` 放 Designer 文件，`src/presentation/` 放页面 C++ 响应与展示；`.h/.cpp` 按模块同目录维护。UI 负责人共同维护这两部分及 styles/resources，业务与网络代码分别归逻辑和网络负责人。较早目录描述若有歧义，以该规范为准。

本文件只记录当前仓库成员共同需要遵守的运行环境、构建方式、协作和安全规则。

## 运行环境

- 目标运行环境是 VMware 虚拟机 `BitDev`：Ubuntu 22.04 64 位、2 vCPU、8192 MB 内存、NAT 网络，客户机用户为 `bit`。
- 项目统一使用 Qt 6 开发和验收。BitDev 已安装 Qt 6.2.4，qmake 入口为 `/usr/bin/qmake6`；不得再使用默认 `/usr/bin/qmake`（Qt 5.15.3）构建本项目，也不得以 Qt 5 的构建或测试结果作为验收依据。
- 2026-09-05 宿主机检查结果：`ChargingUser.pro` 当前仅声明 `QT += widgets network`、C++17，未声明 `webenginewidgets`；仓库源码和工程文件中暂未发现 WebEngine 地图实现。
- 宿主机 Anaconda 自带的 `qmake.exe` 是 Qt 5，仅可辅助查看文件，不能用于本项目构建。Qt 6 构建、测试和 UI 验收必须在 BitDev 中使用 `/usr/bin/qmake6` 完成。
- 2026-09-05 BitDev 实测结果：Ubuntu 22.04.3 LTS；Qt 6.2.4；`/usr/bin/qmake6`、`qt6-base-dev`、`qt6-declarative-dev` 和 `qt6-tools-dev` 已安装；共享项目目录 `/mnt/hgfs/WorkSpace/QtProjects/APP/ChargingUser` 可访问。
- 宿主机普通权限运行 `vmrun` 时，可能无法枚举由高权限 VMware 进程启动的虚拟机，并误报 `Total running VMs: 0` 或 `The virtual machine is not powered on`。检查虚拟机、读取客户机 IP 和后续 `vmrun -T ws ...` 操作必须使用宿主机高权限执行；先用高权限 `vmrun -T ws list` 核对，再用高权限 `vmrun -T ws getGuestIPAddress <vmx> -wait` 动态获取 IP。不得因普通权限下的误报擅自启动第二个实例、结束 VMware 进程或删除 `.lck` 文件。
- 腾讯地图接入前置条件：协议冻结后，在上述 BitDev 环境中确认 Qt WebEngine 页面实际加载、地图 API Key 注入和坐标/路线功能；地图 Key 只能通过本地未提交配置或环境变量注入，不得写入仓库、日志、测试快照或协议样例。
- Windows 宿主机只作为源码编辑和资料管理环境。

### 虚拟机登录凭据

- 本项目已获得用户授权：凡是需要在 `BitDev` 虚拟机中构建、测试、运行、查看日志或进行 UI 验收的操作，可以直接使用本机凭据文件登录，不必重复询问用户是否使用账号密码。
- 凭据文件位置：`D:\SHIJIAN\WorkSpace\QtProjects\APP\ChargingUser-module-docs\.vm-credentials`。该文件只存在于本地 worktree，已由 `.gitignore` 排除，不得提交、复制到源码/文档或回显其内容。
- 凭据文件中的用户为客户机用户 `bit`；密码只允许由自动化登录过程读取。命令行、日志、截图、提交信息和最终回复中不得显示密码。
- 虚拟机配置文件位置：`D:\SHIJIAN\PATH\Ubuntu2204Bit.zip\Ubuntu2204Bit\BitDev.vmx`。客户机 IP 必须通过 `vmrun -T ws getGuestIPAddress <vmx> -wait` 动态获取，禁止使用历史 IP。
- 该授权仅覆盖虚拟机身份验证，不覆盖删除文件、修改密钥/证书、调整 CI/CD、强制关机、Git 推送或其他需要单独确认的操作。

## 开始开发前

1. 在宿主机确认仓库状态：`git status`、当前分支和已有未提交改动。
2. 检查 VMware 和 `BitDev` 状态；虚拟机 IP 必须动态获取，不得把历史 IP 写死到脚本或配置中。
3. 确认共享目录可用。
4. 通过 SSH 进入客户机后，再识别实际工程入口、Qt Kit、构建系统和依赖。虚拟机未启动、共享目录未挂载或 SSH 未验证时，不宣称已完成客户机构建测试。

## 验证环境硬性要求

- 本项目所有验证必须在 VMware `BitDev` 虚拟机的 Ubuntu 22.04 客户机中完成，包括构建、运行启动、单元测试、集成测试、协议联调、网络连接、资源加载、页面交互、截图验收和性能/兼容性检查。
- Windows 宿主机只用于编辑、文件检查和 Git 操作。Windows 上的编译、运行、静态检查或截图不能单独作为项目验证结论。
- 通过 SSH 在客户机执行命令属于虚拟机验证；需要桌面交互时使用 VMware GUI。验证前必须确认目标客户机、共享源码和 Qt Kit 正确。
- mock、离线测试和协议单元测试也必须在虚拟机中执行；可以不依赖真实服务端，但不能在 Windows 上执行后冒充目标环境验证。
- 报告验证结果时必须写明虚拟机、Ubuntu 版本、Qt/qmake 版本、源码路径、构建/测试命令和结果。若虚拟机未启动、共享目录异常、SSH 未连通或版本未确认，只能报告“未验证”。

## 构建与运行

- 日常构建、测试、日志查看和程序生命周期管理必须在 Ubuntu 客户机 SSH 中完成；需要窗口、布局、地图渲染或弹窗验收时必须使用 VMware GUI。
- 当前工程入口为 `ChargingUser.pro`，应用入口为 `src/main.cpp`，目标名为 `ChargingUserUI`，当前声明 `QT += widgets network`、C++17 和 qmake 构建；上级目录的旧 `ChargingUserUI` 原型不是本仓库构建目标。
- qmake 工程必须明确 Qt 模块和 C++ 标准；正式构建产物放入 `bin/`，中间文件放入 `build/`。Windows 与 Ubuntu 共享源码时只能在 BitDev 中使用 Qt 6 Kit 构建，避免不同 Qt ABI 的对象文件互相污染。`bin/`、`build/`、Qt Creator 用户配置和其他机器生成文件不得提交。
- 构建前确认 `.gitignore` 已覆盖构建产物、本机 Kit 配置、日志、临时文件和敏感配置；源码、`.ui`、`.qss`、`.qrc`、必要项目文件和测试代码必须保留。
- UI 和业务实现的每次可交付变更都必须在 Ubuntu 22.04 / Qt 6.2.4 目标环境使用 `qmake6` 完成相称的构建和测试验证；不能以 Windows、Qt 5 或其他 Qt 版本的结果替代。

## 注释与代码可读性原则

- 必要注释必须写。凡是仅靠代码结构、命名或 Qt API 不能直接理解的设计意图、业务约束、状态迁移、协议字段、线程边界、异步回调、幂等/重试规则、异常恢复和兼容性处理，都必须在紧邻代码处说明。
- 对外公开的类、函数、信号、槽、DTO 字段和状态枚举必须说明用途、输入/输出、单位、可空性、线程要求及失败语义；头文件接口优先使用简洁的 Doxygen 风格注释。
- 复杂算法、非显然条件、临时兼容逻辑和与服务端协议对应的魔法值必须解释“为什么这样做”和适用边界；协议消息码、错误码、超时值和状态值不得只留下裸数字。
- 注释必须与实现同步更新，不得保留描述旧行为的误导性注释；发现已有注释与代码不一致时，在修改相关代码的同一变更中修正。
- 不写重复代码的注释、逐行翻译语法的注释或掩盖坏命名的长注释。优先通过清晰命名、拆分函数和类型约束提高可读性，再为必要的设计原因补充注释。
- 新增或修改业务逻辑、网络适配、页面状态和共享接口时，至少补充对应的边界、失败路径或状态约束注释，并在虚拟机验证时检查注释所描述的行为仍成立。

## 资源图片格式

- 项目所有资源图片禁止使用 SVG 格式。SVG 在当前 Qt/运行环境中可能出现显示、缩放或资源加载问题，不得新增、继续引用或作为正式 UI 验收资源。
- 图片资源统一使用已在 Ubuntu/Qt 6.2.4 虚拟机中验证过的 PNG、JPG/JPEG 等栅格格式；需要透明背景或图标时优先使用 PNG。
- `.qrc` 中不得登记 SVG 图片；`.ui`、QSS、C++ 和文档不得引用 `.svg` 作为运行时资源。已有 SVG 文件不擅自删除，迁移时应生成等效栅格资源、更新引用并在虚拟机中验证显示，再决定是否清理旧文件。
- 图片替换必须检查资源别名、尺寸、DPI、透明度、加载失败状态和不同页面的显示效果；不能只在 Windows 预览正常就视为通过。

## 双人共享仓库协作

- 本仓库由客户端逻辑负责人和 UI 设计负责人共同使用。修改前先查看 `git status` 和目标文件当前内容，保留协作者已有改动，不覆盖、不回退、不顺手格式化无关文件。
- `.pro`、`resources.qrc`、应用入口、公共 DTO、页面意图信号、渲染状态和控件 `objectName` 是共享接缝；修改这些内容前应与另一位负责人协调，并在变更说明中注明影响。
- UI 负责人维护 `src/presentation/`、`ui/`、`styles/`、图片/图标/字体、资源登记、布局、视觉样式以及加载、空态、错误、禁用和重试等界面响应。逻辑负责人维护 `src/common/`、`src/modules/`、业务状态机、服务、网络边界、缓存恢复和测试。
- `src/app/` 是装配层：只创建对象、连接 UI 意图与业务接口、把业务结果转换为页面渲染状态；不得承载业务规则或具体控件操作。该目录属于公共接缝，修改时按小提交协作。
- Widget 只负责采集输入、发出语义化用户意图和渲染传入状态；不得直接访问业务 Service、socket、JSON、数据库或承担权威业务计算。业务逻辑不得依赖 `QWidget`、`Ui::*`、控件指针、`objectName` 或页面编号。
- UI 可使用 mock 独立开发，但 mock 必须集中在测试/演示适配器中，不得把固定登录结果、余额、订单或站点数据写入正式业务逻辑。
- 未经明确约定，不自行创建或切换团队分支。Codex 不自动执行 `commit`、`push`、`pull`、`merge`、`rebase` 或冲突解决；需要提交时先展示变更摘要。

## TODO：UI 响应层重构（UI 负责人）

> 本项由 UI 负责人在 `user-design-main` 分支完成。目标是将 UI 响应代码从业务逻辑中物理分离；逻辑负责人不得代为移动或重写现有窗口文件，除非双方先确认公共接缝。

目标结构：

```text
src/
├── presentation/                  # UI 负责人维护
│   ├── contracts/                  # SubmitState、LoginViewState、ProfileEditViewState
│   ├── pages/                      # LoginPage、ProfileEditPage、MainWindow 等 QWidget
│   ├── widgets/                    # 可复用纯展示组件
│   └── presentation.pri
├── app/                           # 公共装配接缝
│   ├── application.h/.cpp         # 创建正式对象
│   ├── useruibinder.h/.cpp        # 连接 UI 意图、M1 结果和 M4 流程
│   └── app.pri
├── modules/                       # 逻辑负责人维护：领域和业务服务
├── flow/                          # M4：页面流转、恢复
└── network/                       # 网络负责人维护
```

UI 必须完成：

1. 将现有窗口源码按页面逐步迁入 `src/presentation/pages/`，同步维护 `presentation.pri`；不得一次性批量移动并覆盖逻辑分支文件。
2. 页面只读取输入、发出语义化意图并通过 `render(const XxxViewState &state)` 渲染；加载、错误提示、空态、按钮禁用、重试、Frozen/Unknown 限制提示均在 `presentation/` 实现。
3. 登录页发出 `loginRequested(const QString &phone)`，资料页发出 `profileSaveRequested(const QString &nickname)`；不得继续使用无参数成功信号驱动业务跳转。
4. 保持登录页 `editPhoneNumber`、`btnLogin`，并补充 `errorLabel`、`loadingIndicator`；资料页也补充错误和加载控件。修改冻结的 signal、ViewState、`objectName`、资源别名或 `.pro` 前先同步逻辑负责人。
5. Widget 不得持有或调用 `IUserService`、`IUserNetworkApi`、`UserService`、`QTcpSocket`，不得解析 JSON、协议码或自行判定新用户/Frozen/页面跳转。
6. 使用独立演示装配器或 Mock 覆盖 Idle、Loading、校验失败、网络失败、服务端失败、结果未知、Frozen、长文本和资源缺失；演示数据不得进入正式 Widget 或 `main.cpp` 默认路径。

完成标准：

- UI 层仅依赖稳定的页面意图与 ViewState 合同；业务层不依赖 `QWidget`、`Ui::*`、控件指针或页面编号。
- `src/app/UserUiBinder` 成为页面与 `IUserService` 的唯一正式连接点，M4 负责页面分流。
- 在 Ubuntu 22.04 / Qt 6.2.4 目标环境使用 `qmake6` 完成构建、资源加载、各 UI 状态和交互验证。
- 详细接口与迁移任务见 `docs/ui-collaboration-guide.md` 和 `docs/user-module-contract.md` 第 6 节。

## 用户模块开发基线

- 客户端总体架构以 `docs/02概要设计说明书第8组--客户端综合设计.docx` 为上位设计，用户模块细化合同为 `docs/user-module-contract.md`；两者冲突时先执行综合设计并同步修订细化合同。
- UI 设计使用 `user-design-main`，逻辑开发使用 `user-module-main`；两者当前共同基线为提交 `2cb25c3`。公共接缝变更拆成小提交，禁止用整文件覆盖另一分支成果。
- 客户端按 M1—M4 划分：M1 负责用户身份、资料和会话；M2 负责站点、Charger、地图和充电控制；M3 负责订单、结算与钱包；M4 负责页面流转、跨模块用例、统一错误和结果未知恢复。钱包余额与充值不属于 M1。
- M1 首版包含手机号免密登录/自动注册、内存会话、昵称资料、Frozen 身份状态和退出。首版不实现验证码、密码登录、修改手机号、头像上传或 Token 持久化。
- 已确认：`autoRegistered` 作为新用户标识，`username` 作为首版用户 ID；Frozen 用户允许登录、查询、充值和支付，但禁止开始新充电和新建预约。金额整数分规则由 M3 与共享合同维护。
- 普通用户资料查询、昵称修改和退出应答必须使用服务端专用接口。消息码未冻结前只允许在 `MockUserNetworkApi` 中模拟，不得使用 `GETDATA(100)` 或高权限 `UPDDATA(111)` 绕过接口缺口。
- M1 业务接口命名为 `IUserService`，网络边界命名为 `IUserNetworkApi`；页面只提交登录、资料和退出意图，业务由 `UserService` 处理，页面分流由 M4 的 `IAppFlowCoordinator` 处理，`src/app/` 负责连接这些接口。
- 所有异步请求使用 `requestId`；修改资料、充值、启动、停止、结算等变更操作还必须使用 `operationId`。变更结果未知时由 M4 通过 `IPendingOperationStore` 保存并查询结果，禁止盲目重试。
- 登录页正式 objectName 为 `editPhoneNumber`、`btnLogin`；登录后的首页、充电、我的使用 `MainWindow + QStackedWidget` 和固定底部导航。现有 `.ui` 名称由 UI 分支统一迁移，逻辑分支不单独改名。
- 首批逻辑代码按 `src/common/`、`src/modules/user/`、`src/flow/` 和领域网络接口组织；UI 代码逐步迁入 `src/presentation/`，装配代码进入 `src/app/`。不提前实现未冻结的真实协议适配。下一步文件清单和验收条件以用户模块合同为准，UI 工作接缝以 `docs/ui-collaboration-guide.md` 为准。

## 网络与安全边界

- 网络同学已交付根目录下的 `protocol.h`、`massagehandler.h/.cpp` 和 `协议与接口说明.md`。前三个文件是客户端/服务端共享的 v2 协议源码，类名 `MassageHandler` 为既有兼容命名；未经网络负责人同步确认，不重命名、不单方面修改帧格式、消息码或字段语义。
- `MassageHandler` 只负责 `pack()`、`feed()`、分片重组和 `frameReady(msgType, payload)`，不负责 `QTcpSocket`、连接状态、发送队列、心跳定时、响应超时、请求关联或重连。网络负责人应在 `BackendClient`/传输实现中补齐这些职责，M1—M3 不重复实现连接管理。
- 协议帧头固定为大端 `4B msgType + 8B msgSize`，JSON 使用 UTF-8 Compact 格式；单物理帧载荷上限 8MB，超过 16KB 使用 400/401/402 分片。断线或 socket 复用前必须在所属线程调用 `MassageHandler::reset()`。
- 网络对象和 `MassageHandler` 必须在同一工作线程创建和使用；`readyRead -> feed -> frameReady` 后再由领域适配层解析业务字段，UI 更新只能回到主线程。
- 客户端每 30 秒发送 107 空载荷心跳，断线后每 5 秒尝试重连；重连成功后重新执行 116 手机号登录并查询服务端权威状态。`SERVER_IP` 和 `SERVER_PORT` 只是可配置默认值，不得被当成固定部署地址。
- 当前 v2 帧和业务载荷没有 `requestId`、`operationId` 和原请求类型，统一 3xx 错误无法可靠关联并发请求或恢复结果未知的变更操作。真实领域协议适配必须等待服务端补齐关联字段和按 `operationId` 查询结果的能力；在此之前只能使用 Mock，不能用页面当前状态猜测响应归属。
- 客户端使用领域网络适配层封装协议请求；UI 不直接调用 `MassageHandler`、暴露消息码或依赖服务端自由文本错误。
- 网络传输端允许客户端自行构建业务接口和请求映射，但新增消息码、响应字段、跨表事务、幂等处理或服务端业务分支必须同步服务端并完成真实联调。高权限不等于客户端直连 SQLite 或绕过服务端权威状态。
- 不在仓库、源码、日志、测试快照或文档中保存密码、Token、地图 Key、服务器密钥、默认管理凭据或完整未脱敏报文。
- 本机 VMware 登录信息只允许保存在仓库根目录的本地 `.vm-credentials` 文件中；该文件已加入 `.gitignore`。需要进行虚拟机操作时可直接读取使用，但不得复制到源码、文档、日志、测试快照或任何提交内容中，自动化读取后不得回显密码。
- 任何修改 `.env`、密钥、证书、CI/CD 配置、远端仓库或生产环境的操作，都必须先获得明确授权。

## 收尾检查

- 记录虚拟机中的实际构建环境、构建命令、成功/失败结果和未验证项；宿主机结果只能作为辅助信息。
- 运行结束后报告虚拟机是否仍在运行；除非用户明确要求，不强制关闭或重启虚拟机。
- 如需重启或关闭虚拟机，先确认 Qt Creator 等客户机程序没有未保存内容，并完成共享目录检查；不得在共享目录异常时继续操作。
