# 地图 UI 协作清单

## 目的

本文明确地图导航逻辑已经完成的部分，以及 UI 同学需要配合的界面、资源和验收工作。业务层通过现有 DTO、Binder 和 Qt 信号交互，UI 不应直接调用腾讯接口或拼接供应商 JSON。

## 当前已完成

- `TencentMapService`：腾讯 WebService 地理编码、路线请求、错误分类和配额错误处理。
- `MapUiBinder`：请求上下文、结果关联、重试/取消和导航状态管理。
- `TencentMapBridge`：受控 WebChannel 接口，支持 ready、加载失败、站点点击、视野变化和快照更新。
- `InteractiveMapWidget`：站点经纬度快照、站点标记同步、选中回传和降级画布。
- `RoutePreviewWidget`：路线折线同步到腾讯地图，WebEngine 不可用或无 Key 时保留自绘路线。
- Qt 工程：WebEngine/WebChannel 为可选模块；缺少模块时仍应正常显示主界面。

## 需要 UI 同学配合的事项

### 1. 地图容器与布局

- 保持 `mapView` 和 `routeMap` 的 objectName 不变，避免破坏现有 Binder 查找和 UI 测试。
- 为地图容器预留完整可用尺寸，WebEngine 视图应覆盖地图区域但不能遮挡定位、搜索区域、错误提示和重试按钮。
- 确认窗口缩放时地图视图同步 resize；不能依赖固定 390×354 尺寸。

### 2. 状态面板

地图需要明确展示以下状态：加载中、加载失败、无 Key/无 WebEngine 降级、定位失败、路线规划中、路线为空、配额耗尽。

- 重试按钮只触发已有 retry 信号，不得伪造 `mapReady`。
- 地图失败时，站点列表、站点详情和充电入口仍保持可用。
- 配额耗尽文案应提示稍后重试，不要显示为“网络断开”。

### 3. 站点标记视觉规范

- 可用、不可用、选中状态使用现有资源或 UI 统一资源，不在 JS 中硬编码业务颜色。
- 点击标记后回传 `stationId`，由页面统一更新列表选中态。
- 标记数量较多时需确认重绘性能和聚合策略；首版先支持常规站点数量。

### 4. 路线展示

- 驾车和步行路线使用不同图例或颜色，并与路线模式按钮保持一致。
- 路线为空、路线错误时清除旧路线，不能继续显示上一条路线。
- 起点、终点、路线折线的坐标必须来自当前 `NavigationViewState`，不能使用页面缓存的旧站点坐标。
- “查看路线步骤”与地图折线状态保持同步；路线加载期间禁用不适用操作。

### 5. 腾讯 Key 与运行配置

- UI 不保存、不提交 Key；本地通过 `TENCENT_MAP_KEY` 或 `config/tencent-map.local.json` 配置。
- 无 Key 时必须验证降级界面可用；不要因为地图不可用阻塞登录、首页和站点列表。
- UI 联调时记录浏览器控制台错误、地图加载失败消息和当前 provider，便于区分 Key、配额、域名白名单和布局问题。

## UI 使用的现有接口

### C++ 组件接口

```cpp
InteractiveMapWidget::setMarkers(const QList<Marker> &markers);
InteractiveMapWidget::setSelectedStation(const QString &stationId);
InteractiveMapWidget::setRoutePolyline(const QVector<GeoPoint> &polyline);
InteractiveMapWidget::renderMapStatus(MapLoadStatus status, const QString &, bool canRetry);
InteractiveMapWidget::renderLocationStatus(MapLoadStatus status, const QString &, bool canRetry);
InteractiveMapWidget::setViewportBounds(const std::optional<GeoBounds> &bounds);
```

### UI 信号

```cpp
markerSelected(QString stationId);
locateRequested();
searchAreaRequested(GeoBounds bounds);
mapReady();
```

页面应通过 `MainWindow`、`NavigationWindow` 和 `IMapUiBinder` 使用这些接口，不应直接访问 `TencentMapService`、`QWebEnginePage` 或 WebChannel 对象。

## 协作边界

| 内容 | 负责方 |
| --- | --- |
| 腾讯 API、坐标转换、请求取消、错误分类 | 逻辑/服务侧 |
| ViewState 到控件的映射 | UI/应用集成侧 |
| 地图布局、状态文案、图标和颜色 | UI 侧 |
| WebChannel 方法和 JSON 字段 | 共同评审，逻辑侧维护协议 |
| Key、本地配置、配额信息 | 本地运行配置，禁止提交 |
| 自动化测试和回归脚本 | 共同维护 |

## 验收清单

- [ ] 无 WebEngine、无 Key 时主界面能打开并显示降级地图。
- [ ] 有 WebEngine 和有效 Key 时腾讯底图能加载并回报 ready。
- [ ] 站点列表、地图标记和选中态双向同步。
- [ ] 路线加载、成功、为空、失败、重试状态显示一致。
- [ ] 地图失败不影响站点列表和充电入口。
- [ ] 窗口缩放、返回首页、切换导航模式后无旧视图残留。
- [ ] 不向 Git 提交 Key、Cookie 或 WebEngine 调试缓存。

## 当前已知限制

- `TencentMapBridge` 已接入，但路线页面的起点/终点图标和自动视野调整仍需 UI 联调。
- 腾讯 JS API 的域名白名单、配额和真实网络可用性需要在目标机器单独验收。
- BitDev 已安装 Qt 6.2.4 WebEngine/WebChannel；其他 Qt Kit 依赖版本需单独记录。
