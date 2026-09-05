# 地图与导航双人协作约定

日期：2026-09-05；代码核对基线：`56a5614`。状态：**并行开发约定草案；完成第 9.1 节的接口冻结提交后开始并行编码**。

本文供 UI 负责人和逻辑负责人共同开发首页地图、站点详情与路线导航使用。已有事实、设计要求和新增接口建议分别标明；新增接口不能被当作已有代码调用。

## 1. 依据与本轮范围

- [客户端综合设计](02概要设计说明书第8组--客户端综合设计.docx) 第 2.3、3.2、4、5、6 节：腾讯地图 JavaScript API、QWebEngineView、GCJ-02、驾车/步行路线、地图与列表联动；地图失败不得阻断站点文字列表及充电入口。
- [目录规范](project-structure.md)、[UI 协作指南](ui-collaboration-guide.md)：presentation 发意图并渲染状态，app 装配，M2 提供服务，M4 决定跨页面流转。
- [用户模块合同](user-module-contract.md)：M1 提供会话和账号状态；地图属于 M2；引入 WebEngine 前确认目标 Kit。
- [用户 Demo](user-demo.md)：当前 Mock 用户流程不代表真实地图或服务端联调完成。

本轮交付：定位或手动选择起点 → 附近站点查询/搜索 → 地图与列表联动 → 指定站点详情 → 驾车/步行路线规划、展示及返回。

“导航”首轮指路线预览、距离、预计时长与步骤展示。实时跟随定位、语音播报、偏航重算、后台定位和外部地图 App 唤起不默认包含，需要另行约定。充电启动只交接意图，不能借地图开发伪造订单或启动成功。

## 2. 当前实际代码

| 项目 | 现状 |
|---|---|
| 首页 | 位于 `ui/shell/mainwindow.ui` 与 `pages/shell/mainwindow.*`，地图为图片，站点为固定展示；尚无独立 HomePage |
| 站点详情 | `src/presentation/pages/home/stationdetailwindow.*` 与 `ui/home/stationdetailwindow.ui` 已存在 |
| 地图导航 | `src/presentation/pages/home/navigationwindow.*` 与 `ui/home/navigationwindow.ui` 已存在 |
| 页面容器 | `MainWindow::renderSecondaryPage(QWidget*)`，二级页进入同一 pageStack |
| Demo | 已连接站点详情、地图导航和返回；当前忽略 stationId，不能表示已经查询指定站点 |
| 业务与地图 | 当前 `src/modules/` 只有 user；IMapService、IChargerService 在概要设计中有名称，尚无对应实现 |
| Qt 模块 | 正式工程当前为 widgets、network，尚未登记 webenginewidgets/webchannel |

现有信号必须兼容迁移：

```cpp
// MainWindow
void stationDetailsRequested(const QString &stationId);
// StationDetailWindow
void backRequested();
void navigationRequested();
void chargeRequested();
// NavigationWindow
void backRequested();
```

`navigationRequested()` 无参数时，装配层必须从当前详情上下文取得真实 stationId；没有已加载站点时禁用入口。不得用卡片行号、名称或固定坐标代替 ID。

## 3. 分工与文件所有权

| 负责人 | 负责内容 | 建议位置（新路径待确认） |
|---|---|---|
| UI | 页面布局、地图容器、标记/路线绘制、加载/错误/空态、用户输入、返回意图 | 现有 `pages/home/`、`ui/home/`；首页暂留 shell，拆分另做提交 |
| UI | 可复用 MapViewWidget、受控 WebChannel 展示桥、地图 HTML/JS | `src/presentation/widgets/map/`、`resources/map/` |
| UI 主维护，双方冻结 | 页面 ViewState、展示用标记及路线类型 | `src/presentation/contracts/mapviewstates.h` |
| 逻辑 | 站点/Charger 查询、筛选、数据有效性与业务权限 | `src/modules/charger/`，IChargerService |
| 逻辑 | 定位来源、地址解析、坐标转换、路线请求与结果规范化 | `src/modules/map/`，IMapService、供应商适配器 |
| 逻辑主接入，双方评审 | 页面与服务连接、DTO 转 ViewState、当前站点上下文 | `src/app/mapuibinder.*`、`src/app/imapuibinder.h` |
| 逻辑 | 返回来源、跨模块充电入口及会话失效处理 | `src/flow/` 的 M4 扩展 |
| 网络/逻辑 | 站点协议适配、超时、请求关联、供应商请求边界 | `src/network/` 与 IChargerNetworkApi |

共同文件 `.pro/.pri`、qrc、公共类型、信号和 objectName 由一人编辑、另一人评审；不得同时改同一个共享文件。逻辑层不接收 QWidget、QWebEngineView 或 Ui 指针；页面不直接调用 Service、Socket 或拼业务 JSON。

地图 JS 允许 SDK 绘制地图与接收地图手势；业务查询经 Binder → M2 → 适配器。若供应商 SDK 必须在 JS 发出定位/路线请求，应把该能力实现为受控供应商桥，由逻辑层发起带 requestId 的请求，回传规范化结果；不能让页面点击直接承担查询规则。具体桥方案在真实接入前冻结。

## 4. 拟议数据合同

以下名称和字段是新增建议，不是现有头文件。领域类型放 common/modules，展示类型放 presentation/contracts；业务层不包含展示头文件。

| 类型 | 最少字段与语义 |
|---|---|
| GeoPoint | `double latitude, longitude`；单位度；已规范化为 GCJ-02；可缺失位置使用 optional，不以 (0,0) 表示未知 |
| GeoBounds | `southWest, northEast`；纬度 [-90,90]、经度 [-180,180]，有限数；跨日期变更线范围首轮拒绝并提示，不静默翻转 |
| LocationResult | `point, accuracyMeters, capturedAtUtc, source`；source 区分设备/手动；无精度使用 optional |
| GeocodeResult | `candidates`；每项包含稳定候选 ID、名称、完整地址和 point；零项为空结果，多项由用户选择后再规划路线 |
| StationQuery | `optional center, optional bounds, keyword, cursor, pageSize`；center/bounds 二选一；半径如使用以米表示，范围/分页上限由服务验证 |
| StationSummary | `stationId, name, address, optional point, optional distanceMeters, availableCount, totalCount`；价格来源与单位单独明确，展示文案不参与计算 |
| StationDetail | `stationId, summary, chargers, updatedAtUtc`；Charger 含稳定 chargerId、状态、可操作标记及原因 |
| TravelMode | `Driving, Walking`；供应商不支持时返回明确错误，不暗中切换方式 |
| RouteQuery | `origin, destination, stationId, mode`；目的地取当前站点权威坐标 |
| RouteResult | `routeId, stationId, mode, origin, destination, polyline, distanceMeters, durationSeconds, steps`；polyline 为有序 GeoPoint 列表，steps 含指令文案与可选距离 |

坐标字段始终使用 latitude/longitude 命名，不使用含糊 x/y。供应商数组顺序由适配器转换。原始 WGS-84 或其他坐标必须附来源，由逻辑侧统一转为 GCJ-02，已转换数据不得重复转换。用户拒绝定位或 VM 不支持定位时提供手动起点，不把深圳默认中心伪装为当前位置。

## 5. 页面与 Binder 接口候选

页面发意图，Binder 接收同名槽；以下新增接口需双方签字确认后落头文件。保留现有窗口类名，不要求先拆分所有页面。

```cpp
// 首页：当前由 MainWindow 暴露
void locateRequested();
void stationSearchRequested(const QString &keyword);
void searchAreaRequested(const GeoBounds &bounds);
void stationSelected(const QString &stationId); // 标记选择、列表同步
// stationDetailsRequested(stationId) 已存在：打开详情

// 站点详情：新增含明确方式的意图，旧 navigationRequested 暂由适配层接入
void routePreviewRequested(TravelMode mode);
void stationRefreshRequested();

// NavigationWindow 新增
void routeModeRequested(TravelMode mode);
void manualOriginRequested(const QString &address);
void routeRetryRequested();
// backRequested() 已存在

// 展示入口，均为新增
void MainWindow::renderHome(const HomeMapViewState &state);
void StationDetailWindow::render(const StationDetailViewState &state);
void NavigationWindow::render(const NavigationViewState &state);
```

旧 navigationRequested 可临时映射为 Driving；新增入口启用后断开旧连接，不能同一次点击提交两次。默认方式最终需双方确认。chargeRequested 暂保留，后续携带 chargerId 的新接口由充电合同定义；未选择 Charger 时不能默认取第一项启动。

拟议 IMapUiBinder 对外发出 `homeStateChanged(state)`、`stationDetailStateChanged(state)`、`navigationStateChanged(state)`，以及 `pageRequested(MapPageTarget target, QString stationId)`。MapPageTarget 建议 Home/StationDetail/Navigation；由 app 适配现有 M4，不把该枚举直接塞入 M1 用户导航枚举。Widget 指针只在展示装配处使用。

| ViewState | 必需内容 |
|---|---|
| HomeMapViewState | 独立的 mapStatus/locationStatus/stationsStatus、markers、stations、selectedStationId、camera、可选当前位置、搜索草稿、消息与重试能力 |
| StationDetailViewState | stationId、详情、status、message、canRetry、canNavigate、canCharge、disabledReason |
| NavigationViewState | stationId、起终点文案与坐标、mode、routeStatus、可选 route、message、canRetry、canChangeMode |

展示状态建议 Idle/Loading/Ready/Empty/Error，并通过能力字段控制按钮。地图加载失败与站点查询失败分别展示；路线查询是只读操作，不套用充值等变更操作的 ResultUnknown 语义。刷新时可保留旧数据但明确“正在更新/数据可能过期”；切换目的地后不得把上一目的地路线当作新结果。

render 不发请求、不发意图；程序更新地图中心/标记不得回触发查询。地图拖动仅显示“搜索当前地图区域”，点击后才发查询。地图 ready 前缓存最后一份展示快照，ready 后重放一次，避免不断新增标记或重复连接。

## 6. M2 与供应商异步接口候选

沿用现有 `RequestContext`、`ClientError`。以下是 QObject 槽/信号的签名草案，相关 DTO 待定义；不规定服务端消息码。

```cpp
// IChargerService：调用
void queryStations(const RequestContext &ctx, const StationQuery &query);
void queryStationDetail(const RequestContext &ctx, const QString &stationId);
// 结果信号
void stationsReady(const RequestContext &ctx, const StationPage &page);
void stationDetailReady(const RequestContext &ctx, const StationDetail &detail);

// IMapService：调用
void locate(const RequestContext &ctx);
void geocode(const RequestContext &ctx, const QString &address);
void planRoute(const RequestContext &ctx, const RouteQuery &query);
void cancel(const QString &requestId);
// 结果信号
void locationReady(const RequestContext &ctx, const LocationResult &result);
void geocodeReady(const RequestContext &ctx, const GeocodeResult &result);
void routeReady(const RequestContext &ctx, const RouteResult &result);

// 两个服务分别提供
void requestFailed(const ClientError &error);
```

StationPage 至少包含 items、nextCursor、hasMore；地址解析多结果时应由逻辑返回候选供选择，不能任意选择同名地点；NavigationViewState 需包含候选列表，页面补充 originCandidateSelected(candidateId) 意图。查询由调用方在发送前生成非空 requestId；只读请求 operationId 为空。失败必须回填 requestId；不通过“当前页面”猜测响应归属。

Binder 按定位、列表、详情、路线分别保存最新 requestId 和会话/页面世代。快速切站、切出行方式、返回或退出后，迟到结果不得覆盖当前界面。cancel 为尽力取消，取消后仍须丢弃迟到回调。当前请求成功/失败只终结一次；超时、防重、限次重试由逻辑/适配器负责。

地图服务密钥缺失、无定位权限、无定位来源、无路线、网络失败、供应商限流和无效坐标需要可区分错误；Binder 把 ClientError 转为 UI 文案和 canRetry，页面不解析供应商错误码。读取失败不清空搜索草稿。

## 7. 页面流程与降级

1. 首页显示时请求地图初始化与站点数据，各自独立显示状态。定位失败可选手动地点或浏览地图。
2. 地图区域必须支持用户按下拖动和平移；拖动结束后保留新视野并显示“搜索当前地图区域”，不能在每个移动事件中连续请求站点。
3. 地图标记与下方站点行必须使用同一个稳定 `stationId`。点击地图标记后，页面发出 `stationSelected(stationId)`，把对应站点行从当前位置移动到列表第一行，并以边框、阴影或底色形成“浮起”选中态；不得仅改变地图标记而让列表保持无反馈。
4. “提起显示”会改变当前展示顺序：最新选中的站点始终位于第一行，其余站点保持移动前的相对顺序，不复制站点行。再次选择其他标记时，取消旧行高亮并把新站点移动到第一行。页面必须按 `stationId` 移动已有数据，不能通过重新请求或名称匹配实现。程序为了同步列表而移动地图或调整列表时不得再次发出选择意图，避免循环。
5. 点击下方站点行时执行相反联动：选中同 stationId 的地图标记，并把地图平移到该站点；点击行内进入箭头或明确的详情区域才发出 `stationDetailsRequested(stationId)`。若产品最终决定整行进入详情，须在冻结记录中统一，不能同一版本出现两种行为。
6. 详情加载成功且坐标有效后允许规划路线。起点未知先提示定位或手动地址；不使用 (0,0)。
7. 切换驾车/步行产生新请求。路线空结果显示“暂无可用路线”；失败提供重试并保留起终点。
8. 导航返回详情保留 stationId；详情返回首页保留地图视野、当前第一行的选中站点与筛选。M4 维护来源，app 调用 pageStack 展示入口。
9. 地图瓦片/脚本/路线失败时仍展示站点文字、地址及允许的充电入口。Frozen 禁止开始新充电和新预约，但不能据此禁止浏览地图；Unknown 权限由逻辑给出，UI 只渲染。

## 8. WebEngine、资源与安全协作

- 概要设计指定腾讯地图 JS API；具体 SDK 版本、定位提供方式、路线 API/配额和授权配置尚未核实。真实接入前由逻辑负责人核对官方资料并记录，不能凭此草案声称已支持。
- 在 BitDev 检查目标 Qt Kit 是否包含 WebEngine/WebChannel，再由指定一人登记对应模块。近期 Qt 6.2.4 Demo 构建成功不代表 WebEngine 可用；团队文档仍要求 Qt 5.15.3，两者差异需共同确认，不能静默改验收基线。
- HTML/JS 展示资源由 UI 维护并登记 qrc；图标使用 PNG/JPG，不新增 SVG 运行资源。地图版权/供应商标识按授权要求保留。
- WebChannel 只暴露地图 ready、选中 stationId、用户视野变化等最小接口，校验 ID、坐标和数据长度；不得暴露任意文件、命令、通用网络请求或完整 Binder。
- 第三方返回文案作为文本展示，不能未经处理拼入 JS/HTML。限制页面导航及桥可访问来源；页面刷新/销毁时解绑回调。
- Key 使用本地排除配置，通过受控装配注入；前端 JS Key 可见，应使用供应商访问限制与配额，不能把服务端密钥塞进页面。配置缺失显示明确错误，不写默认密钥。
- 不在提交、日志、截图中保存 Key、用户精确位置或完整路线请求。UI 更新在主线程；跨线程 DTO 注册 Qt 元类型，销毁后不得回调页面。

## 9. 交付顺序与验收

### 9.1 并行开发启动门槛

两人不能从仅有自然语言描述的接口各自开工。开始并行编码前，先由一人创建一个只包含公共合同的提交，另一人审阅后共同记录其提交号。该提交必须包含：

- `GeoPoint`、`GeoBounds`、`TravelMode`、Station/Route DTO 的唯一声明位置与最终字段。
- `HomeMapViewState`、`StationDetailViewState`、`NavigationViewState` 的唯一声明位置与最终字段。
- 页面意图、IChargerService、IMapService 和 IMapUiBinder 的最终函数签名。
- Qt 元类型声明、可空字段表示方式、距离/时长单位、GCJ-02 坐标约定。
- Mock 接口及最小固定数据，不包含真实 Key、网络协议码或供应商实现。

两条开发分支必须从同一个“接口冻结提交”创建。冻结后公共头文件只允许通过单独的 `contract:` 提交修改；任何一方需要改接口时先暂停依赖该接口的新代码，由另一方审阅并同步 cherry-pick 同一提交。禁止在两个分支分别修改相同公共头文件后再依赖 Git 自动合并。

推荐分支关系：

```text
user-design-main（共同基线 + 接口冻结提交）
├── feature/map-navigation-ui       UI 负责人
└── feature/map-navigation-logic    逻辑负责人
```

每人使用独立 clone 或 Git worktree。不能让两个 Qt Creator 实例同时编辑同一个物理工作目录，也不能在一方构建期间由另一方切换该目录分支。两个工作目录使用各自独立的源码外构建目录。

### 9.2 并行文件分配

接口冻结后，双方按下表开发。未列入自己一侧的文件视为只读；确需修改时交给集成阶段，或先创建小型合同变更提交供双方同步。

| UI 分支可修改 | 逻辑分支可修改 | 并行期间双方均不修改 |
|---|---|---|
| `src/presentation/pages/home/**` | `src/modules/map/**` | `ChargingUser.pro` |
| `src/presentation/widgets/map/**` | `src/modules/charger/**` | `src/presentation/presentation.pri` 及各方根聚合 `.pri` |
| `ui/home/**` | 对应模块内部测试 | `src/app/**` |
| `styles/**` | 逻辑模块自己的 `.pri` | `src/flow/**` |
| 新地图 PNG/JPG、独立地图 HTML/JS | Mock 服务实现与领域适配器 | `resources/resources.qrc`、`src/main.cpp`、`src/demo/**` |
| UI 分支自己的展示测试/预览工程 | 网络侧新增实现须另行与网络负责人分配 | 本文第 4—6 节定义的公共合同头文件 |

具体要求：

- UI 分支仅依赖冻结的 ViewState 和 Mock 快照，不创建同名 IMapService/IChargerService 替身；页面测试可以使用局部 test fixture，但不得复制公共 DTO 定义。
- 逻辑分支实现领域 DTO 与 Service，不依赖 presentation 的 ViewState，也不包含 QWidget、Ui、QWebEngineView；ViewState 映射由后续 app 集成提交完成。逻辑测试使用 fake network/observer 验证领域结果。
- UI 新资源先放入明确目录但不改 qrc；逻辑新增 `.pri` 可以在本模块内维护，但不改顶层 `.pro`。这些登记由集成负责人一次完成。
- `MainWindow` 当前仍承载首页。UI 如需拆成 HomePage，必须在 UI 分支内保持原公共信号兼容，并把正式装配调整留给集成提交。

### 9.3 各自提交粒度

UI 推荐提交顺序：

1. `feat(ui): add map and navigation view states rendering`
2. `feat(ui): add map widget and station list interaction`
3. `feat(ui): complete station detail and route views`
4. `test(ui): cover map loading empty and error states`

逻辑推荐提交顺序：

1. `feat(map): add location and route service contracts`
2. `feat(charger): add station query service`
3. `feat(map): add mock map and station providers`
4. `test(map): cover request correlation and stale responses`

一个提交不得同时包含公共接口改名、目录移动和实际功能。每个提交说明依赖的接口冻结提交号、涉及的状态与已运行测试。

### 9.4 集成负责人和唯一共享文件窗口

两边各自通过后，指定一名集成负责人创建临时集成分支。建议先合并逻辑分支，再合并 UI 分支；这个顺序只用于让依赖关系清晰，不代表逻辑可以覆盖 UI 文件。合并前双方工作区必须干净，并确认两条分支都包含相同接口冻结提交。

只有集成负责人在该阶段修改以下共享接缝：

- `ChargingUser.pro`、各聚合 `.pri`、`resources.qrc`。
- `src/app/mapuibinder.*`、`src/app/imapuibinder.h` 及 `src/app/app.pri`。
- `src/flow/` 中页面目标和返回来源接线。
- `src/demo/` 中 Mock 页面装配。
- 因首页拆分所需的 `MainWindow` 最小适配。

集成提交只做装配、工程登记和必要适配；发现合同不兼容时退回合同变更流程，不在 Binder 中用类型转换、固定 ID、页面编号或默认坐标掩盖差异。

### 9.5 合并前自动检查

每个分支交付前至少执行：

```bash
git diff --check FREEZE_COMMIT...HEAD
git diff --name-only FREEZE_COMMIT...HEAD
```

执行时把 `FREEZE_COMMIT` 替换为第 10 节记录的提交号。第二条输出必须符合第 9.2 节分配。若两分支除冻结合同外修改了同一路径，先由双方决定唯一保留版本，再进入集成；不能直接接受冲突标记或任选 ours/theirs。

集成后在 BitDev 执行全新 qmake 构建，运行双方单元测试和 Mock 交互测试，再进行 VMware GUI 地图验收。qmake 成功、测试成功和 GUI 成功分别记录，不能互相替代。推送公共分支前执行一次 fetch，并确认远程共同分支仍是预期祖先；禁止 force push。

这里的流程能消除已知的并行覆盖和重复定义风险，但任何文档都不能保证实现绝对无缺陷。是否可合并以编译、自动测试、GUI 验收和远程祖先检查四项结果为准。

### 9.6 功能交付阶段

| 阶段 | UI 负责人 | 逻辑负责人 | 共同完成标准 |
|---|---|---|---|
| A 接口冻结 | 确认窗口、信号、ViewState、控件命名 | 确认 DTO、requestId、服务签名、供应商方案 | 双方在本文件记录确认结果，不重复定义类型 |
| B Mock 贯通 | render、空态/错误、容器、标记/列表联动 | 集中 Mock 服务与 Binder/M4 接线 | 指定站点与路线结果可控，不卡住用户链路 |
| C 真实地图 | MapViewWidget 与 JS 展示桥 | 定位、坐标转换、地址解析、路线/站点适配 | 真实供应商与真实站点数据分开记录联调结果 |
| D 回归交接 | GUI 点击、布局、长文案及资源 | 异步、取消、超时、权限与日志测试 | BitDev 记录版本、命令、结果、截图与未验证项 |

最低验收清单：

- [ ] 首页真实地图与站点列表独立加载；无站点空态、地图失败仍能看文字详情。
- [ ] 标记和列表 stationId 一致；三个不同站点不会显示同一份固定详情。
- [ ] 地图可拖动；拖动期间不连续请求，结束后可显式搜索当前视野。
- [ ] 点击任一标记时，对应站点行移动到列表第一行并呈浮起选中态；其余站点保持相对顺序，切换标记后旧选中态清除且列表不产生重复项。
- [ ] 点击站点行时对应标记选中并居中；程序同步不会形成“标记选择 → 行选择 → 标记选择”的重复信号循环。
- [ ] 定位允许/拒绝/不可用均可操作；手动地址及无效坐标有明确处理。
- [ ] 驾车、步行各验证一条路线；距离以米、时长以秒传输，UI 展示单位正确。
- [ ] 快速切站/切方式、返回后迟到响应、退出再登录不串数据；render 不造成重复请求。
- [ ] 返回保留正确来源、地图视野和选中站点；二级页在同一容器。
- [ ] 网络失败、Key 缺失、地图加载失败、无路线、超时可恢复；不伪造充电成功。
- [ ] 登录、资料、钱包现有链路无回归；Qt 5/Qt 6 及 WebEngine 实际版本分开记录。

每次交接附源码提交号、接口变更、构建命令、BitDev/Ubuntu/Qt 版本、通过项和未验证项。Windows 文件检查不是运行验收。本文件仅新增设计约定，未执行运行测试。

## 10. 冻结与并行记录

待双方确认：SDK/定位方案、GeoPoint 及分页类型、地址多候选回调、旧导航信号兼容期限、出行默认方式、IMapUiBinder 名称、M4 扩展方式、Qt/WebEngine 验收 Kit、共享文件编辑人。

共同起始提交：`56a5614`。

接口冻结提交：待填写。

UI 分支及负责人：`feature/map-navigation-ui` / 待填写。

逻辑分支及负责人：`feature/map-navigation-logic` / 待填写。

集成分支及负责人：待填写。

UI 确认人/日期：待填写。逻辑确认人/日期：待填写。

开发前核对实际分支与远程引用，不把旧文档中的共同基线当作当前 HEAD。UI 与逻辑各在约定分支工作，合并前先交换小提交；不要整文件覆盖 `.pro`、MainWindow 或 Binder。迁移、接口、UI 和真实适配分开提交。Qt Creator `.pro.user`、构建产物、本机测试夹具与 AGENTme.md 保持本地；伙伴已纳入版本管理的 tests 不擅自移除。提交、推送由用户明确发起，不强推公共分支。
