## 目录与职责规范（2026-09-05）

本规范是两条分支共同执行的目录约定；目标目录不代表已完成迁移。

| 目录 | 内容及负责人 |
|---|---|
| `ui/` | 根目录 Qt Designer `.ui`：布局、静态文字、控件属性；UI 负责人 |
| `styles/`、`resources/` | QSS、图片、图标、字体、qrc；UI 负责人 |
| `src/presentation/contracts/` | 共同确认的 ViewState 展示合同；UI 主维护 |
| `src/presentation/pages/` | 页面 .h/.cpp、输入采集、意图信号、render 状态渲染；UI 负责人 |
| `src/presentation/widgets/` | 自定义绘制、动画、动态布局、复用控件 .h/.cpp；UI 负责人 |
| `src/app/` | 对象创建、页面与服务连接、业务结果到 ViewState 映射；双方协调，逻辑负责正式接入 |
| `src/modules/`、`src/common/` | 业务规则、会话、公共领域值类型；逻辑负责人 |
| `src/flow/` | M4 导航决策、跨模块流程和结果未知恢复；逻辑负责人 |
| `src/network/` | 传输、协议适配；网络负责人 |

- .h 与 .cpp 按模块同目录存放，不建立全局头文件集中目录。
- 根目录 `ui/` 保留 Designer 文件；需要 C++ 的展示行为统一放到 presentation，不把这些 C++ 放进根目录 ui。
- presentation 负责加载、错误、空态、按钮禁用、输入保留、重试入口和 Frozen/Unknown 提示。它只发用户意图并渲染 ViewState，不调用 Service、Socket、JSON 或协议宏。
- 业务手机号校验、重复请求控制、登录、会话、资料更新归 M1；UI 的输入辅助不能替代业务校验。
- 新老用户与会话失效的跳转决策归 M4；presentation 根据装配层传入的导航意图执行页面显示，不自行推断业务结果。
- Page 发出意图 → app 连接 IUserService；业务结果 → app 映射 ViewState → Page.render。业务层不包含 Widget、Ui::*、objectName 或页面序号。
- render 不发起业务请求、不回发提交信号；同步输入时按需阻断控件信号，并避免状态刷新覆盖用户正在编辑的草稿。
- UI 更新在主线程执行。请求超时与重试策略由业务/网络层控制，UI 不设置模拟业务完成的定时器。
- 登录后使用 MainWindow + QStackedWidget；固定一级导航为首页、充电、我的，二级页面进入同一容器。
- 登录意图为 loginRequested(phone)，保存意图为 profileSaveRequested(nickname)。loginSucceeded/profileSaved 等业务成功应由业务结果驱动，不能由按钮点击伪造。
- 登录控件 editPhoneNumber、btnLogin 为目标冻结名称，旧页面由 UI 分支迁移；errorLabel、loadingIndicator 及 ViewState 字段由双方确认后实现。
- 昵称更新成功后，仅更新昵称展示；不要把部分应答中的空手机号或 Unknown 状态覆盖完整用户资料。账户状态依赖业务层权威状态。
- UI 演示入口与正式装配分开，用固定 ViewState 测试页面；演示数据不写入正式 Widget。
- .pro、main.cpp、公共信号、ViewState、资源别名是共同接缝；迁移逐页完成，并同步 HEADERS/SOURCES/FORMS/RESOURCES，避免重复登记；不手工维护 uic 生成头文件。
- 所有运行、构建、测试、资源和交互验收都在 BitDev / Ubuntu 22.04 / Qt 5.15.3 完成，宿主机文件检查不代表目标环境验证。

## TODO：UI 负责人执行清单

- [ ] 创建 presentation/contracts、pages、widgets 和 presentation.pri。
- [ ] 逐页迁移现有 src/*window.h/.cpp；ui/*.ui 保留原目录，类名无必要不改。
- [ ] 登录和资料页实现语义化意图及 render(ViewState)，补齐加载/错误/输入保留。
- [ ] 覆盖 Idle、Loading、Success、ValidationError、NetworkError、ServerError、ResultUnknown 及 Frozen/Unknown。
- [ ] 与逻辑负责人确认 app 绑定接口，消除页面内的业务判断和演示成功跳转。
- [ ] 在目标虚拟机验收，记录构建命令、测试结果与未验证项。
- [ ] 同步 docs/ui-collaboration-guide.md 和 docs/user-module-contract.md；目录迁移与业务接入分别提交。

UI 分支可能尚未包含 M1 和真实适配器源码；文档描述的是共同合同，不要求 UI 同学复制实现。逻辑分支负责后续正式装配。
