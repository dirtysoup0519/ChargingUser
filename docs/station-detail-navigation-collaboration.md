# 站点详情与路线导航并行开发约定

日期：2026-09-07；共同基线：`a6f148d`；依赖地图合同冻结提交：`34a4094`。

本文用于 UI 负责人和逻辑负责人并行完成“首页站点 → 站点详情 → 路线预览 → 返回”的功能。上位合同是 [地图与导航最终合同](map-navigation-collaboration.md)，公共类型以 `src/presentation/contracts/mapviewstates.h`、`src/modules/map/maptypes.h`、`src/modules/charger/chargertypes.h` 和 `src/app/imapuibinder.h` 为唯一声明。本文只细化实现与验收，不复制或重新定义公共 DTO。

若本文与地图最终合同或公共头文件冲突，以已冻结的公共头文件为准，并通过单独的 `contract:` 提交同步修订本文。双方不得在各自分支创建同名替代类型或自行改变函数签名。

## 1. 本阶段范围

本阶段完成：

```text
首页点击“查看详情”
  → 按 stationId 加载站点详情
  → 展示站点摘要、价格、充电桩列表及全部页面状态
  → 请求驾车或步行路线预览
  → 展示起终点、距离、预计时长、路线和步骤
  → 返回详情或首页并保留上下文
```

本阶段不包含：

- 启动充电、创建订单、占用充电桩或结算。
- 实时跟随定位、语音播报、偏航重算和后台导航。
- 唤起外部地图 App。
- 客户端自行计算价格、可充电权限或路线。
- 腾讯地图 Key、服务端消息码和真实协议字段的单方面决定。

“路线导航”在本阶段表示路线预览。现有 `startButton` 不得伪造“开始导航成功”；在实时导航合同冻结前应禁用、隐藏或改为不会产生业务结果的“查看路线步骤”。

## 2. 当前代码事实

| 页面 | 当前文件 | 当前缺口 |
|---|---|---|
| 站点详情 | `src/presentation/pages/home/stationdetailwindow.*`、`ui/home/stationdetailwindow.ui` | **旧占位实现，不能作为最终方案**：当前写死站名、价格和三个桩；必须改为由 `StationDetailViewState.chargers` 动态渲染任意数量 |
| 路线导航 | `src/presentation/pages/home/navigationwindow.*`、`ui/home/navigationwindow.ui` | **旧占位实现，不能作为最终方案**：当前只有固定路线图片和文案；必须改为由 `NavigationViewState` 渲染未来 API 返回的路线数据 |
| 页面容器 | `MainWindow::registerSecondaryPage/renderSecondaryPage` | 已可装入二级页，页面目标仍由 Binder/M4 决定 |
| 公共合同 | `mapviewstates.h`、`imapuibinder.h` | 已冻结；本阶段必须直接使用 |

现有 `navigationRequested()`、`chargeRequested()` 是早期兼容信号。新增冻结接口接通后，同一次点击只能走一条连接，禁止旧信号与新信号同时触发。

本表只记录重构开始前的代码现状，不表示允许保留固定数据。首页站点数量和一个站点内部的充电桩数量都不得写死；固定三个站点、三个充电桩和固定路线图全部属于待替换的演示占位。

## 3. 文件所有权与并行边界

### UI 负责人修改

```text
src/presentation/pages/home/stationdetailwindow.h/.cpp
src/presentation/pages/home/navigationwindow.h/.cpp
ui/home/stationdetailwindow.ui
ui/home/navigationwindow.ui
src/presentation/widgets/map/**          # 仅路线/地图展示组件
styles/**
本地排除的 UI 预览夹具
```

UI 负责人负责布局、控件状态、动态列表、加载/空态/错误态、禁用原因、输入保留和语义意图，不调用 Service、网络、JSON、数据库或页面编号。

### 逻辑负责人修改

```text
src/modules/charger/**
src/modules/map/**
对应模块测试与 Mock 实现
```

逻辑负责人负责详情与路线查询、requestId、取消、超时、迟到响应隔离、坐标与单位转换、权限结果和错误分类，不包含 QWidget、Ui 指针或 objectName。

### 集成阶段由唯一负责人修改

```text
src/app/**
src/flow/**
src/demo/**
ChargingUser.pro
src/presentation/presentation.pri 及聚合 .pri
resources/resources.qrc
```

公共文件若必须变化，先创建独立小提交并让双方同步。禁止两条分支分别修改 `mapviewstates.h`、`imapuibinder.h` 后依赖 Git 自动合并。

## 4. 冻结页面接口

### 4.1 StationDetailWindow

UI 展示入口：

```cpp
void render(const StationDetailViewState &state);
```

UI 用户意图：

```cpp
void backRequested();
void stationRefreshRequested();
void routePreviewRequested(TravelMode mode);
```

语义：

- `backRequested()`：只表达返回，不决定返回首页还是其他来源。
- `stationRefreshRequested()`：用户显式刷新当前 stationId；页面不自行生成请求。
- `routePreviewRequested(mode)`：请求当前详情站点的路线预览；目的地 stationId 和坐标由 Binder 当前上下文提供。
- 默认出行方式为 `TravelMode::Driving`。
- 未加载合法 stationId、`canNavigate=false` 或详情仍 Loading 时，路线按钮必须禁用且不得发信号。

### 4.2 NavigationWindow

UI 展示入口：

```cpp
void render(const NavigationViewState &state);
```

UI 用户意图：

```cpp
void backRequested();
void routeModeRequested(TravelMode mode);
void manualOriginRequested(const QString &address);
void originCandidateSelected(const QString &candidateId);
void routeRetryRequested();
```

语义：

- 驾车与步行按钮只在用户实际切换时发出一次 `routeModeRequested(mode)`。
- `render()` 设置按钮选中态时必须阻断信号，防止形成请求循环。
- 手动起点提交前只做 trim；空输入不提交并显示本地必填提示。
- 地址解析返回多个候选时显示候选列表，用户选择后只传稳定 candidateId。
- 失败重试只发 `routeRetryRequested()`；页面不保存或重新拼装 RouteQuery。
- `backRequested()` 由 Binder/M4 根据来源返回站点详情，并保留 stationId。

### 4.3 Binder 对接

集成阶段连接关系固定为：

```text
MainWindow::stationDetailsRequested(stationId)
  → IMapUiBinder::stationDetailsRequested(stationId)

StationDetailWindow::stationRefreshRequested()
  → IMapUiBinder::stationRefreshRequested()

StationDetailWindow::routePreviewRequested(mode)
  → IMapUiBinder::routePreviewRequested(mode)

NavigationWindow::routeModeRequested(mode)
  → IMapUiBinder::routeModeRequested(mode)

NavigationWindow::manualOriginRequested(address)
  → IMapUiBinder::manualOriginRequested(address)

NavigationWindow::originCandidateSelected(candidateId)
  → IMapUiBinder::originCandidateSelected(candidateId)

NavigationWindow::routeRetryRequested()
  → IMapUiBinder::routeRetryRequested()

两个页面的 backRequested()
  → IMapUiBinder::backRequested()

IMapUiBinder::stationDetailStateChanged(state)
  → StationDetailWindow::render(state)

IMapUiBinder::navigationStateChanged(state)
  → NavigationWindow::render(state)

IMapUiBinder::pageRequested(target, stationId)
  → app/M4 控制 MainWindow 页面容器
```

UI 不直接连接 `IChargerService` 或 `IMapService`。Binder 不操作页面内部控件。

## 5. 站点详情渲染规则

`StationDetailViewState` 是页面唯一输入。页面不得保留一份与 ViewState 竞争的权威站点数据。

| status | 页面行为 |
|---|---|
| Idle | 显示详情骨架或等待提示；导航、刷新和充电相关入口按能力字段控制 |
| Loading | 显示加载反馈；保留旧内容时标明正在更新；禁止重复刷新和路线请求 |
| Ready | 渲染 stationId 对应的名称、地址、价格、可用情况和动态充电桩列表 |
| Empty | 显示“暂无站点详情/暂无充电桩”；仍允许返回；是否可重试由 `canRetry` 决定 |
| Error | 显示 Binder 提供的 message；旧内容可保留但不得伪装为最新；按 `canRetry` 显示重试 |

具体要求：

- `stationId` 为空时不得打开路线或充电入口。
- 名称、地址、价格、可用状态均直接使用展示字段，不在 UI 重新计算。
- `chargers` 必须动态渲染，数量不设固定上限；至少覆盖 0、1、3、20+ 项，不依赖 `charger1/2/3`、固定数组长度或列表下标。
- 每个充电桩行使用稳定 `chargerId`；状态文案、功率、`canCharge` 和 `disabledReason` 来自 ViewState。
- 刷新时按 `chargerId` 创建、更新和删除行；不得把固定三行隐藏/复用成最多三个结果。
- 长列表允许滚动，不压缩行高，不遮挡底部操作区。
- `canNavigate=false` 时禁用路线按钮并显示 `disabledReason` 或页面 message 中的原因。
- `canCharge=false` 时不得发出充电行为；Frozen/Unknown 等限制只按 ViewState 渲染，UI 不推断账号权限。
- 快速切换 stationId 时，页面只能显示最新 state；迟到响应过滤由 Binder 负责。

### 5.1 充电入口暂缓合同

冻结的地图合同没有选中 chargerId，也没有携带 `stationId + chargerId` 的启动充电意图。现有无参数 `chargeRequested()` 无法可靠表示用户选择了哪个桩。

因此本阶段可以完成充电桩列表和可用/禁用视觉，但不得把“选择充电桩并充电”接到真实业务。后续充电合同至少需要明确：

```cpp
void chargerSelected(const QString &chargerId);
void chargeConfirmationRequested(const QString &stationId,
                                 const QString &chargerId);
```

以上签名目前只是待冻结候选，不能在 UI 与逻辑分支分别落地。充电确认页启动前应单独评审并提交 `contract:` 变更。

## 6. 路线导航渲染规则

`NavigationViewState` 是页面唯一输入。

| routeStatus | 页面行为 |
|---|---|
| Idle | 显示起终点；等待用户确认起点或选择方式 |
| Loading | 保留起终点并显示路线计算中；禁用重复切换或按 `canChangeMode` 控制 |
| Ready | 展示路线、距离、预计时长和动态步骤列表 |
| Empty | 显示“暂无可用路线”；保留起终点和方式；允许返回或更换起点/方式 |
| Error | 显示 message；按 `canRetry` 展示重试；不得清空用户手动输入 |

具体要求：

- 起点和终点分别使用 `originText`、`destinationText`，不把默认深圳位置伪装为当前位置。
- `origin` 或 `destination` 缺失时不绘制虚假坐标；提示定位或手动输入。
- 驾车/步行选中态完全由 `mode` 驱动。
- `route.distanceText`、`durationText` 直接展示；UI 不以经纬度自行计算。
- `route.polyline` 为空时不得声称路线成功；由 Binder 映射为 Empty 或明确 message。
- `route.steps` 动态渲染，支持长列表滚动。
- `originCandidates` 多于一个时显示候选选择区域；不自动取同名地点第一项。
- 切换 mode、起点或候选后，旧路线可保留为灰化历史，但必须明确正在更新，不能当作新请求结果。
- 地图绘制只接受规范化 GCJ-02 数据，不重复转换坐标。
- 地图加载失败时仍显示文字起终点、距离、时长和路线步骤。

### 6.1 为未来地图 API 保留的正式边界

导航页不得把 `route_map.png` 当作权威路线。该图片只能在 Mock 或 API 尚未返回时作为视觉占位；进入 Ready 后，路线展示组件必须使用 `NavigationViewState.route` 中的数据：

```text
IMapService::planRoute(RequestContext, RouteQuery)
  → routeReady(RequestContext, RouteResult)
  → IMapUiBinder 校验 requestId 并转换展示字段
  → navigationStateChanged(NavigationViewState)
  → NavigationWindow::render(state)
  → 地图展示组件绘制 route.polyline
```

未来接入腾讯地图或其他供应商时，只替换 `IMapService` 的适配器和受控地图展示桥，不修改页面意图、`NavigationWindow::render()` 或业务层接口。UI 只消费已经规范化为 GCJ-02 的 `GeoPoint`、距离文案、时长文案和步骤，不直接调用供应商 API、不持有 Key、不解析供应商 JSON。

路线展示组件至少预留以下纯展示能力；具体类名可由 UI 分支内部决定，不形成第二套业务合同：

```cpp
void setRoutePolyline(const QVector<GeoPoint> &polyline);
void setOrigin(const std::optional<GeoPoint> &origin);
void setDestination(const std::optional<GeoPoint> &destination);
void clearRoute();
```

若真实地图桥需要异步 ready/loadFailed 或视野事件，继续使用地图最终合同中已经冻结的受控桥事件，不允许页面绕过 `IMapUiBinder` 发起路线请求。

## 7. 页面容器与返回规则

- 站点详情和路线导航都注册到 `MainWindow::pageStack`，不创建互相控制的独立顶层窗口。
- `registerSecondaryPage()` 只注册页面，不自动改变当前页面。
- `pageRequested(StationDetail, stationId)` 显示详情并隐藏底部一级导航。
- `pageRequested(Navigation, stationId)` 显示路线页并保持相同 stationId。
- 导航页返回详情时保留已加载详情和当前 stationId。
- 详情页返回首页时保留搜索词、地图视野、标记选择和第一行站点。
- 页面自身不得调用另一页面的 `show()`、`hide()`、`setCurrentIndex()` 或推断返回目标。
- 会话失效时 M4 可直接导航登录页；页面只渲染最终状态，不拦截权威流程。

## 8. 异步、错误和安全要求

- 详情、路线、定位和地址解析分别维护最新 requestId。
- 页面切换、快速切站、切换方式和退出登录后，迟到结果不得覆盖当前页面。
- `cancel()` 是尽力取消，结果处理仍必须检查 requestId、页面世代和会话世代。
- 只读查询不使用 operationId 或 ResultUnknown。
- 错误文案由 Binder 转换为可展示 message；UI 不解析网络错误码或供应商文本。
- 地图/路线失败不得阻断站点文字详情和返回操作。
- 不在源码、日志、截图或测试夹具中保存地图 Key、用户精确位置、Token 或完整真实报文。
- 所有 QWidget 更新在主线程完成；跨线程 ViewState 使用冻结的 Qt 元类型。

## 9. Mock 与独立开发

UI 分支可以使用本地排除的预览夹具，以固定 `StationDetailViewState` 和 `NavigationViewState` 驱动页面；不得创建第二套公共类型或把 Mock 数据写入正式 Widget。

逻辑分支的 Mock 必须实现正式 `IChargerService`、`IMapService` 接口，并验证 requestId、取消和迟到结果。双方 Mock 不互相依赖，集成时由唯一 Binder 装配。

建议覆盖数据：

- 站点：正常、无坐标、无价格、Frozen/Unknown 限制、长名称和长地址。
- 充电桩：0、1、3、20 项；空闲、使用中、故障、未知状态。
- 路线：驾车成功、步行成功、无路线、网络失败、供应商失败、起点缺失、多地址候选、长步骤列表。
- 异步：快速切站、快速切方式、返回后迟到成功、旧失败晚于新成功。

## 10. 提交与合并顺序

UI 分支建议：

1. `feat(ui): render station detail states`
2. `feat(ui): render route preview states`
3. `test(ui): add local detail and route previews`（仅团队决定纳入版本管理时使用）

逻辑分支建议：

1. `feat(charger): implement station detail queries`
2. `feat(map): implement route preview flow`
3. `test(map): cover detail and route request correlation`

集成阶段：

1. 先确认两边都包含 `34a4094` 及后续共享合同提交。
2. 先合并逻辑，再合并 UI；发生公共头文件差异时退回合同变更，不任选 ours/theirs。
3. 唯一集成负责人连接 Binder、M4、Demo、工程清单和资源。
4. 在 BitDev 全新构建目录运行 qmake、编译、逻辑测试和 GUI 验收。
5. fetch 并确认远程祖先后再推送共享分支，禁止 force push。

## 11. 最低验收清单

- [ ] 从首页任一“查看详情”按钮进入正确 stationId 的详情页。
- [ ] 0、1、3、20+ 个充电桩全部由 ViewState 动态创建，刷新时按 chargerId 增删更新，不越界、不重叠、不遮挡底部按钮。
- [ ] Loading、Ready、Empty、Error 和 retryable/non-retryable 状态均可见且不会重复提交。
- [ ] 站点长名称、长地址、长价格文案不会遮挡或截断关键操作。
- [ ] `canNavigate=false`、`canCharge=false` 和 disabledReason 显示正确。
- [ ] 驾车/步行切换每次只发一个语义意图，render 不回发请求。
- [ ] 起点缺失、手动地址、多候选、无路线和路线失败均有可恢复界面。
- [ ] 路线步骤支持长列表；地图失败时文字路线仍可查看。
- [ ] Ready 状态路线来自 `NavigationViewState.route.polyline`；替换 Mock 为真实 IMapService 适配器时不需要修改页面接口。
- [ ] 导航返回详情、详情返回首页均保留正确 stationId 和来源状态。
- [ ] 快速切站、快速切方式和返回后的迟到结果不会串页。
- [ ] 未接充电合同时不会伪造选择充电桩、启动充电或订单成功。
- [ ] BitDev / Ubuntu 22.04 / Qt 5.15.3 完成构建、资源和 GUI 验收，并记录未验证项。

## 12. 开工门槛

双方开始实现前应共同确认：

1. 本阶段只交付站点详情与路线预览，不包含启动充电和实时导航。
2. 直接使用冻结的 `StationDetailViewState`、`NavigationViewState` 和 `IMapUiBinder`，不新增同名类型。
3. 充电入口保持未接入，直到携带 stationId/chargerId 的充电合同另行冻结。
4. UI、逻辑和集成文件按第 3 节分配，公共文件只有唯一编辑人。
5. UI 分支与逻辑分支从同一共享基线开始，提交保持小而独立。

本文作为双方开工前的共享协作约定，应通过独立文档提交同步到 `user-design-main`。后续接口变化必须使用单独的 `contract:` 提交，不与页面实现混在同一提交中。
