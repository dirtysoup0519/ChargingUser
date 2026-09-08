# 充电桩预约 UI 与逻辑协作约定

日期：2026-09-08  
适用链路：站点详情 → 预约确认 → 预约成功 → 返回站点详情

本文用于 UI 与逻辑并行开发预约功能。公共合同变更先独立提交；页面不得直接读写数据库、修改 Charger 领域状态或自行判定预约成功。

## 1. 产品交互

1. 用户点击可用充电桩整行后，该行高亮。
2. 详情页底部显示两个动作：“前往预约”和“扫码充电”。
3. “扫码充电”沿用现有 `chargeConfirmationRequested(stationId, chargerId)`。
4. “前往预约”发出 `reservationConfirmationRequested(stationId, chargerId)`，由 M4 打开预约确认页。
5. 预约确认页复用充电确认页视觉结构，将费用区改为“预约押金”和“预约时长”。
6. 预约成功由逻辑层返回结果并导航回原站点详情。对应充电桩显示“已预约”，详情顶部显示预约浮窗和剩余时间。

## 2. 冻结候选合同

```cpp
enum class ReservationConfirmationStatus {
    Idle, Loading, Ready, Submitting, Error, ResultUnknown
};

struct ReservationConfirmationViewState {
    QString stationId;
    QString chargerId;
    QString stationName;
    QString stationAddress;
    QString chargerCode;
    QString chargerTypeText;
    QString powerText;
    QString depositText;
    QString durationText;
    int durationSeconds = 0;
    ReservationConfirmationStatus status = ReservationConfirmationStatus::Idle;
    QString message;
    bool canReserve = false;
    bool canRetry = false;
    QString disabledReason;
};

struct ActiveReservationView {
    QString reservationId;
    QString stationId;
    QString chargerId;
    QDateTime expiresAtUtc;
    QString remainingText;
};
```

`reservationId` 和 `expiresAtUtc` 来自逻辑层/服务端。`remainingText` 由 Binder 根据当前时间生成；UI 定时刷新只用于视觉走时，到期后必须发出刷新意图，不能自行释放充电桩。

## 3. 页面意图

```cpp
// StationDetailWindow
void reservationConfirmationRequested(const QString &stationId,
                                      const QString &chargerId);

// ReservationConfirmationWindow
void backRequested();
void reservationRefreshRequested();
void reserveRequested(const QString &stationId,
                     const QString &chargerId,
                     int durationSeconds);
```

页面不生成 requestId、operationId 或 reservationId。Submitting/ResultUnknown 时禁止重复预约。Frozen、已有预约、桩不可用、押金不足等权限均由 Binder 输出 `canReserve/disabledReason`。

## 4. 逻辑与 Demo 数据

Demo 数据放在 `src/demo/reservation-demo-data.tmp`，经 loader 注入 Mock Reservation Service，再由 Binder 生成 ViewState。建议字段：押金整数分、可选预约时长秒数、默认时长、预约结果和到期时间策略。

真实链路为：

```text
后端预约接口 → ReservationService → ReservationUiBinder
→ ReservationConfirmationViewState / StationDetailViewState → UI
```

预约成功后，Map/Station Binder 必须重新查询或合并权威详情，使对应 chargerId 变为 Reserved；页面不得直接改 `map-demo-data.tmp` 或本地列表冒充后端成功。

## 5. 详情页新增展示字段

`StationDetailViewState` 增加可空 `activeReservation`。存在有效预约时：

- 顶部浮窗显示已预约的充电桩编号和剩余时间；
- 对应行显示“已预约”；
- 其他不可预约动作按 Binder 权限禁用；
- 倒计时到零只发出 `reservationExpiredRefreshRequested()`，由逻辑层刷新详情。

## 6. 文件所有权与合并

- UI：`src/presentation/contracts/reservationviewstates.h`、`pages/charging/reservationconfirmationwindow.*`、`ui/charging/**`、QSS。
- 逻辑：`src/modules/reservation/**`、Mock、网络适配和测试。
- 共享：`src/app/**`、`src/flow/**`、Demo 装配、`.pri/.pro`、`resources.qrc`，由唯一集成人修改。
- 合并顺序：合同 → 逻辑 → UI → 装配。不得在两条分支创建同名不同字段类型。

## 7. 验收

- 可用桩整行选择；不可用桩不响应。
- 选择后两个动作显示且携带同一 stationId/chargerId。
- 预约页覆盖 Loading、Ready、Submitting、Error、ResultUnknown。
- 预约成功返回原详情并显示正确桩状态、预约 ID 和倒计时。
- 切页再返回、刷新和程序恢复后倒计时基于 expiresAtUtc，不从初始秒数重启。
- 到期、取消或服务端变更后由权威刷新清除浮窗并恢复桩状态。
