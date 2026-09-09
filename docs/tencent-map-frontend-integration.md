# 腾讯地图前端直连与 UI 接入协议

日期：2026-09-07。状态：**实施决策已确认，新增公共接口仍需在编码前单独冻结。**

本文是 [地图与导航最终合同](map-navigation-collaboration.md) 的供应商接入实施细则。公共 DTO、页面意图、请求关联和页面流转仍以最终合同为准；本文只确定腾讯地图由 Qt 前端直连、UI 如何接入真实地图，以及对应的实施顺序。

## 1. 已确认决策

1. 后端只提供充电站业务数据和目的地坐标，不代理腾讯地图请求。
2. 腾讯 Key、路线请求、地理编码、错误映射、本地限流和供应商替换集中在 Qt 前端。
3. 腾讯能力仍统一经过 `IMapService`；页面和 `MapUiBinder` 不拼腾讯 URL、不解析腾讯 JSON。
4. 第一阶段的“导航”是路线预览：起终点、折线、距离、预计时长和步骤。连续定位、偏航重算、语音播报和后台导航不在本阶段。
5. 腾讯 Key 放在前端意味着可被提取。本方案适用于课程、实习和内部演示，不声称具备生产密钥安全性。
6. 业务请求优先由 `QNetworkAccessManager` 直连腾讯 WebService；腾讯 JavaScript API 仅负责底图、标记、视野和路线绘制，不让页面 JS 承担业务状态机。

推荐链路：

```text
后端站点接口 ──> StationDetail/StationSummary ──> MapUiBinder
                                                    │
设备定位/手动起点 ──> IMapService ──> TencentMapService
                                      │
                                      └──> 腾讯 WebService
                                                │
                                                └──> RouteResult
                                                          │
                                                          └──> NavigationViewState
                                                                    │
                                                                    └──> TencentMapWidget
```

## 2. 后端数据边界

后端不能只返回两个没有语义的数字。站点响应至少包含：

```json
{
  "stationId": "station-001",
  "name": "市民中心充电站",
  "address": "深圳市福田区福中三路",
  "destination": {
    "latitude": 22.54343,
    "longitude": 114.05956,
    "coordinateSystem": "GCJ02",
    "pointType": "entrance"
  },
  "updatedAt": "2026-09-07T10:00:00Z"
}
```

约束：

- `stationId` 是跨列表、标记、详情和路线的唯一身份，不能用名称或坐标代替。
- `coordinateSystem` 必须显式为 `GCJ02` 或 `WGS84`；缺失时响应视为不可导航，不能猜测。
- `pointType` 首版接受 `entrance` 和 `center`。优先使用车辆入口；只有中心点时 UI 显示“目的地为站点中心，入口位置可能有偏差”。
- 网络适配器将原始坐标转换并校验后，才生成公共 `GeoPoint`。公共 `GeoPoint` 继续表示已规范化的 GCJ-02，避免业务层重复转换。
- 未知坐标使用空值，不使用 `(0,0)`、深圳中心或其他默认点。

## 3. UI 调整

### 3.1 首页地图

现有 `InteractiveMapWidget` 是图片背景和归一化坐标自绘，只适合作为 Mock/无 WebEngine 降级视图。真实接入需要新增 `TencentMapWidget`，并在同一地图容器内按能力选择真实地图或降级视图。

首页需要增加：

- 顶部不设置城市入口，只保留自适应搜索输入框和右侧固定搜索按钮；定位入口独立放在地图右下角。
- 地图脚本加载中的遮罩和文案，不能覆盖下方站点列表。
- 地图加载失败提示、重试按钮和“继续使用站点列表”的降级入口。
- 腾讯地图版权和供应商标识区域，不得遮挡或裁剪。
- 当前定位点、定位精度圈、站点标记、选中标记四种独立展示。
- 用户拖动结束后显示“搜索当前区域”；程序居中或缩放不得显示该按钮。
- 真实 `GeoBounds` 回传，替换当前只移动图片、不能产生新经纬度范围的行为。
- Key 缺失或鉴权失败时显示可理解的配置错误，不显示完整 Key、请求 URL或供应商原始响应。

### 3.2 站点详情

详情页保持状态驱动，但增加目的地质量提示：

- 坐标有效且为入口点：允许路线预览。
- 坐标有效但为中心点：允许路线预览，同时显示入口可能偏差。
- 坐标系缺失、坐标非法或坐标为空：禁用路线按钮并说明原因。
- 地图展示失败不能影响站点文字信息和允许的充电入口。

这需要在站点网络适配阶段保留 `pointType`。如果暂不修改公共 `StationSummary`，可先将提示映射为 `StationDetailViewState.message`；正式实现前再决定是否增加专门的 `destinationHint` 字段。

### 3.3 路线预览页

现有 `RoutePreviewWidget` 只按经纬度包围盒自绘折线，没有道路底图。真实页面调整为：

- 地图区域使用 `TencentMapWidget` 绘制腾讯底图、起点、终点和路线折线。
- 地图状态与路线状态分开：底图失败时仍保留路线距离、时间和文字步骤；路线失败时底图仍可浏览。
- 保留驾车/步行切换、手动起点、地址候选、重试和步骤列表。
- 路线请求期间保留上一条路线，但显示“正在重新规划”，并禁止重复提交同一方式。
- 路线成功后自动执行一次 `FitRoute`，普通 `render()` 不反复改变用户视野。
- 当前按钮文案继续使用“查看路线步骤”，不能写成“开始导航”，因为本阶段没有实时导航能力。
- 如果以后增加外部腾讯地图 App 唤起，应新增独立的 `externalNavigationRequested()`，不能复用“查看路线步骤”。

### 3.4 开发诊断

Debug 构建允许显示不含敏感信息的诊断面板：供应商、请求阶段、项目错误码、耗时和剩余本地节流时间。Release 构建不显示请求 URL、Key、精确起点或完整腾讯响应。

## 4. 接口规划

### 4.1 保持不变的业务接口

以下现有接口继续使用，不因腾讯接入而改名：

```cpp
// IMapService
void locate(const RequestContext &context);
void geocode(const RequestContext &context, const QString &address);
void planRoute(const RequestContext &context, const RouteQuery &query);
void cancel(const QString &requestId);

void locationReady(const RequestContext &context,
                   const LocationResult &result);
void geocodeReady(const RequestContext &context,
                  const GeocodeResult &result);
void routeReady(const RequestContext &context,
                const RouteResult &result);
void requestFailed(const ClientError &error);
```

`MapUiBinder` 继续负责 requestId、当前 stationId、当前出行方式、取消和迟到响应过滤。`TencentMapService` 不判断当前页面，也不直接操作 QWidget。

### 4.2 前端腾讯配置

新增仅在模块内部使用的配置类型：

```cpp
struct TencentMapConfig
{
    QString key;
    QUrl webServiceBaseUrl;
    int requestTimeoutMs = 8000;
    int maxRequestsPerMinute = 50;
    bool enableTraffic = false;

    bool isValid() const;
};
```

配置加载规则：

1. Debug 默认读取 `config/tencent-map.local.json`。
2. 仓库只保存不含真实 Key 的 `config/tencent-map.example.json`。
3. `tencent-map.local.json` 必须排除版本控制。
4. Key 不进入 qrc、源码、日志、截图和测试夹具。
5. 配置缺失由 `TencentMapService` 返回 `map-config-missing`，UI 显示配置指引。

这只能防止误提交，不能防止发布后的客户端 Key 被提取。

### 4.3 腾讯业务适配器

新增：

```cpp
class TencentMapService final : public IMapService
{
    Q_OBJECT
public:
    TencentMapService(QNetworkAccessManager *network,
                      const TencentMapConfig &config,
                      QObject *parent = nullptr);

    void locate(const RequestContext &context) override;
    void geocode(const RequestContext &context,
                 const QString &address) override;
    void planRoute(const RequestContext &context,
                   const RouteQuery &query) override;
    void cancel(const QString &requestId) override;
};
```

实现责任：

- 构造腾讯请求并限制请求参数长度。
- 验证起终点均为有效 GCJ-02。
- 解析腾讯结果并生成 `LocationResult`、`GeocodeResult`、`RouteResult`。
- 将腾讯错误转换为稳定的 `ClientError`，页面不得解析供应商错误码。
- 用 requestId 关联 `QNetworkReply`，取消时调用 `abort()`；即使取消失败也只允许一个终态。
- 本地缓存相同起终点与方式的短期路线；对搜索、地理编码和路线分别节流。
- 不在日志中输出带 Key 的 URL、精确起点和完整响应。

首版稳定错误码：

| code | UI 文案方向 | retryable |
|---|---|---:|
| `map-config-missing` | 未配置腾讯地图 Key | false |
| `map-auth-failed` | 腾讯地图鉴权失败，请检查开发配置 | false |
| `map-quota-exceeded` | 地图服务调用额度已用完 | false |
| `map-rate-limited` | 请求过于频繁，请稍后重试 | true |
| `map-network` | 网络不可用，请检查网络后重试 | true |
| `map-timeout` | 地图服务响应超时 | true |
| `map-invalid-coordinate` | 起点或终点坐标无效 | false |
| `map-no-route` | 暂未找到可用路线 | true |
| `map-provider-error` | 地图服务暂时不可用 | true |

### 4.4 地图展示桥

业务接口和地图展示必须分离。新增 presentation 内部接口：

```cpp
enum class MapCanvasFailureKind
{
    ResourceLoadFailed,
    KeyRejected,
    ScriptError
};

struct MapCanvasFailure
{
    MapCanvasFailureKind kind = MapCanvasFailureKind::ResourceLoadFailed;
    QString displayMessage;
    bool retryable = true;
};

class IMapCanvas : public QWidget
{
    Q_OBJECT
public slots:
    virtual void initialize() = 0;
    virtual void reload() = 0;
    virtual void setMarkers(const QVector<MapMarkerView> &markers) = 0;
    virtual void setSelectedStation(const QString &stationId) = 0;
    virtual void setCurrentLocation(const std::optional<GeoPoint> &point) = 0;
    virtual void setRoute(const std::optional<RouteViewData> &route,
                          const std::optional<GeoPoint> &origin,
                          const std::optional<GeoPoint> &destination) = 0;
    virtual void applyCamera(const MapCameraView &camera,
                             const MapCameraCommand &command) = 0;

signals:
    void mapReady();
    void mapLoadFailed(const MapCanvasFailure &failure);
    void markerSelected(const QString &stationId);
    void viewportChanged(const GeoBounds &bounds);
};
```

实现类：

- `TencentMapWidget`：`QWebEngineView + QWebChannel + 腾讯 JS API`。
- `FallbackMapWidget`：由现有 `InteractiveMapWidget` 演进，用于 Mock、无 WebEngine 和地图加载失败时降级。

WebChannel 只传递经过校验的坐标、stationId、标记集合、路线折线和视野。JS 不暴露文件访问、任意 URL 请求、命令执行或完整 Binder 对象。C++ 与 JS 之间的 JSON 由 `TencentMapWidget` 内部序列化，页面和业务层不接触供应商 JSON。

### 4.5 需要评审的公共接口变更

当前 `IMapUiBinder::mapLoadFailed()` 没有错误信息，无法区分资源失败、Key 拒绝和脚本错误。建议冻结为：

```cpp
void mapLoadFailed(const MapCanvasFailure &failure);
```

Binder 将其映射为已有的：

```cpp
HomeMapViewState::mapStatus
HomeMapViewState::mapMessage
HomeMapViewState::canRetryMap
```

除此之外，首版不扩展 `RouteQuery`、`RouteResult` 或 `NavigationViewState`。配额倒计时、实时路况和多路线备选如果以后需要，再通过独立合同变更增加，不能提前塞入字符串字段。

## 5. 前端配额与供应商替换

前端可以实现的是本机调用控制，而不是全体用户的全局额度管理：

- 相同请求短期缓存。
- 输入搜索防抖。
- 路线和地理编码分别使用令牌桶或滑动窗口限流。
- 同类在途请求取消和合并。
- 配额错误后停止自动重试，只保留用户显式重试。
- Debug 面板显示本地调用次数，但不能声称等于腾讯控制台剩余额度。

供应商替换分为两层：

```text
业务请求：IMapService       -> TencentMapService / OtherMapService
地图展示：IMapCanvas        -> TencentMapWidget / OtherMapWidget / FallbackMapWidget
```

如果只替换路线供应商而继续使用腾讯底图，必须确认坐标系和路线折线能在腾讯底图正确对齐；适配器输出仍统一为 GCJ-02。

## 6. 当前代码差距与冲突

| 项目 | 当前代码 | 目标 | 处理 |
|---|---|---|---|
| 路线服务 | `TencentMapService` 已实现地理编码和驾车/步行路线请求 | 真实请求 | 已接入适配器并保留 Mock 测试；仍需真实 Key 人工联调 |
| 首页地图 | 图片背景、自绘标记 | 腾讯底图和真实视野 | 新增 `IMapCanvas/TencentMapWidget` |
| 视野范围 | 降级地图基于站点 bounds 推算拖动范围 | JS 地图回传真实 bounds | Mock 可测试，真实底图仍由展示桥实现 |
| 路线地图 | 自绘折线，无道路底图 | 腾讯底图上绘制路线 | 替换为地图画布，保留文字降级 |
| Key | Demo 支持环境变量运行时注入 | 本地排除配置注入 | 当前不落盘；后续可增加本地配置类型 |
| 错误 | 腾讯网络/超时/频率/供应商/解析错误已映射 | 腾讯错误稳定映射 | 继续细化鉴权与配额错误码 |
| 坐标 | 公共类型直接假定 GCJ-02 | 后端显式声明来源后转换 | 在网络适配边界转换 |
| 导航定义 | 路线预览 | 首版仍为路线预览 | UI 不使用“开始实时导航”文案 |
| Qt 依赖 | `widgets network` | 真实底图需要 WebEngine/WebChannel | 先安装并验证目标 Kit，再改 `.pro` |
| Demo 激活 | 降级地图构造后 ready，失败状态可显示并重载 | 由真实地图加载完成回报 | 腾讯画布接入后移除手工 ready |

### 6.1 本轮修复后的可测试入口

仓库提供 `config/tencent-map.example.json`，本地复制为 `config/tencent-map.local.json` 后填写 Key 并将 `provider` 改为 `tencent`。本地文件已由 `.gitignore` 排除，不进入提交和推送。未提供本地配置时 Demo 使用 `MockMapService`，无需 Key，适合完整 UI 回归。

也可以使用启动进程环境变量覆盖本地配置：

```text
CHARGING_MAP_PROVIDER=tencent
TENCENT_MAP_KEY=<本地腾讯 WebService Key>
TENCENT_MAP_REGION=深圳市  # 可选，默认深圳市
```

Key 只从被忽略的本地配置或进程环境读取，不写入已跟踪文件、qrc 或日志。腾讯适配器当前不提供设备定位；定位失败后首页会继续按默认城市目录加载站点，进入路线页后输入手动起点即可联调地理编码与路线。地址提示显式携带城市范围；`polyline` 按腾讯官方数字数组差分格式解析；缺失/非法候选坐标会被丢弃，避免 `(0,0)` 假坐标。腾讯状态 `121` 映射为不可重试的 `map-quota-exceeded`，鉴权状态 `110/111/112` 映射为 `map-auth-failed`。

当前 BitDev 检查结果：Ubuntu 22.04、Qt 6.2.4；`Qt6WebEngineWidgets`、`Qt6WebChannel`、`Qt6Positioning` 和 `QWebEngineView` 头文件均缺失。这不阻塞 `TencentMapService` 的 WebService 接入，但阻塞真实腾讯 JS 底图和基于 Qt Positioning 的设备定位。已先加入 QtCore-only 的 `TencentMapBridge` 协议桥和 `resources/map/tencent-map.html`，在依赖安装后可直接由 `QWebEngineView + QWebChannel` 注册该桥；安装依赖属于环境准备步骤，不能通过 Mock 测试冒充完成。

## 7. 实现步骤

### 阶段 0：合同变更

1. 确认后端坐标字段、坐标系和入口点语义。
2. 冻结 `TencentMapConfig`、`IMapCanvas`、`MapCanvasFailure`。
3. 评审并修改 `mapLoadFailed(MapCanvasFailure)`。
4. 公共接口变更单独提交，不能与腾讯实现混在同一提交。

完成标准：合同测试在 BitDev 通过，现有 Mock Demo 仍可编译运行。

### 阶段 1：腾讯 WebService 适配器

建议新增：

```text
src/modules/map/tencent/tencentmapconfig.h/.cpp
src/modules/map/tencent/tencentmapservice.h/.cpp
src/modules/map/tencent/tencentresponseparser.h/.cpp
tests/map/tencent-map-service-tests.cpp
config/tencent-map.example.json
```

先实现路线规划，再实现地理编码；定位优先使用 Qt/系统来源，不能把 IP 定位伪装为精确设备定位。

`TencentMapService` 本身不凭腾讯路线接口制造设备位置。建议通过内部依赖注入定位来源：

```cpp
class ILocationProvider : public QObject
{
    Q_OBJECT
public slots:
    virtual void locate(const RequestContext &context) = 0;
    virtual void cancel(const QString &requestId) = 0;

signals:
    void locationReady(const RequestContext &context,
                       const LocationResult &result);
    void locationFailed(const ClientError &error);
};
```

有桌面定位能力时实现 `QtPositioningLocationProvider`；BitDev/虚拟机使用明确返回不可用的 Provider，并让用户走手动地址。不得用腾讯 IP 定位或固定深圳坐标冒充设备精确位置。

完成标准：使用伪造 `QNetworkReply` 覆盖成功、无路线、超时、取消、鉴权失败、配额错误、非法 JSON 和迟到响应；测试不访问公网、不使用真实 Key。

### 阶段 2：真实服务装配

1. 在 Debug Demo 中按配置选择 `MockMapService` 或 `TencentMapService`。
2. 缺 Key 时应用仍能启动，并明确显示配置错误。
3. 保持 `MapUiBinder` 和页面信号不变。
4. 使用真实 Key 进行人工联调，但不记录 Key 和完整请求。

完成标准：真实驾车/步行路线可以转换为当前 `RouteResult`，Mock 回归继续通过。

### 阶段 3：地图画布抽象和降级

1. 从 `InteractiveMapWidget` 提取 `IMapCanvas`。
2. 完成 `FallbackMapWidget`，确保无 WebEngine 时仍可编译和展示站点文字/路线文字。
3. 首页和路线页只依赖 `IMapCanvas`，不直接依赖腾讯类。
4. 增加地图加载、失败、重试和版权区域 UI。

完成标准：用 Fake Canvas 验证标记选择、视野回传、一次性 camera revision 和路线绘制调用。

### 阶段 4：腾讯 JS 地图

前置条件：BitDev 安装与 Qt 6.2.4 Kit 匹配的 WebEngine/WebChannel 开发组件；如果首版包含设备定位，还需安装 Positioning 开发组件并确认虚拟机位置来源。

建议新增：

```text
src/presentation/widgets/map/tencentmapwidget.h/.cpp
src/presentation/widgets/map/tencentmapbridge.h/.cpp
resources/map/tencent/index.html
resources/map/tencent/map.js
```

实现初始化、ready/error、标记、选择、真实 bounds、程序性视野命令、路线和起终点绘制。程序性视野变化必须带内部抑制标记，不能误触发“搜索当前区域”。

完成标准：真实底图加载、地图拖动、标记列表联动、路线拟合和失败降级分别通过 GUI 验收。

### 阶段 5：回归与文档收口

1. 运行地图逻辑、合同、Binder 和 GUI 测试。
2. 在 BitDev 分别记录 qmake 构建、自动测试和有桌面的人工 GUI 验收。
3. 检查仓库历史、日志和截图中没有真实 Key。
4. 更新本文的接口冻结提交、腾讯 API 版本、Key 类型、实际配额和未完成项。

## 8. 验收清单

- [ ] 后端坐标缺少坐标系时禁用路线入口，不猜测坐标系。
- [ ] WGS84 只转换一次，GCJ-02 不重复转换。
- [ ] Key 缺失、错误和额度耗尽有不同提示，均不泄露 Key。
- [ ] 驾车、步行路线使用当前起点和当前 stationId 的权威目的地。
- [ ] 快速切换站点、方式、起点和返回页面不会被旧响应覆盖。
- [ ] 地图脚本失败时站点列表、详情、距离、时间和文字步骤仍可使用。
- [ ] 地图拖动回传真正的 `GeoBounds`，程序性居中不触发区域搜索。
- [ ] 首页标记与站点列表按 stationId 双向联动。
- [ ] 路线成功只执行一次 `FitRoute`，后续 render 不抢回用户视野。
- [ ] Mock 测试不访问公网，真实联调不进入自动测试。
- [ ] BitDev Qt WebEngine/WebChannel 依赖已明确安装和记录版本。
- [ ] UI 不出现“开始导航”误导文案，除非实时导航或外部地图唤起已经实现。

## 9. 暂不包含

- 连续 GPS 跟随和后台定位。
- 偏航检测及自动重新规划。
- 语音导航、车道级提示和实时交通播报。
- 腾讯地图 App 深链唤起。
- 多条备选路线、途经点、车牌限行和新能源偏好。
- 面向生产发布的 Key 保密和全局配额治理。

这些能力如果进入范围，必须先扩展合同和验收标准，不能只改按钮文案或直接调用腾讯 JS。
