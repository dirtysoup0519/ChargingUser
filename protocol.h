#ifndef PROTOCOL_H
#define PROTOCOL_H

/* ================= 自定义应用层协议 =================
 * 帧格式（网络字节序 / 大端）：
 *   +----------------+----------------+----------------------+
 *   | msgType (4B)   | msgSize (8B)   | msg (msgSize 字节)    |
 *   +----------------+----------------+----------------------+
 *   msgType : 消息类型码（三位数字分段，见下方宏定义）
 *   msgSize : msg 字段长度（字节），可为 0（如心跳帧）
 *   msg     : 业务载荷，UTF-8 编码的 JSON 文本（QJsonObject 序列化）
 *
 * 解析端按此格式逐字段解析，数据不足时缓存等待，多余数据留在缓冲区
 * 继续解析下一个协议包，从而正确处理粘包与分次到达。
 * 分片机制：payload 超过 BIGDATA_THRESHOLD 时自动按 400/401/402 分片，
 *          接收端自动重组为完整业务帧，上层无感知。
 *
 * 金额约定（v2）：数据库与业务层统一使用整数"分"（cents），避免浮点误差；
 * 协议载荷同时携带元(double，如 amount)与分(integer，如 amountCents)冗余字段，
 * 客户端展示用元，业务校验用分。
 * ==================================================== */

/* 帧格式常量 */
#define MSG_TYPE_LEN        4       // msgType 字段字节数
#define MSG_SIZE_LEN        8       // msgSize 字段字节数
#define FRAME_HEAD_LEN      (MSG_TYPE_LEN + MSG_SIZE_LEN)   // 帧头 12B
#define MAX_MSG_SIZE        (8 * 1024 * 1024)   // 单帧载荷上限 8MB
#define BIGDATA_THRESHOLD   (16 * 1024)         // 超过 16KB 自动分片
/* 分片重组后的完整业务消息不得超过单帧上限，防止异常分片耗尽内存。 */
#define MAX_ASSEMBLED_MSG_SIZE  MAX_MSG_SIZE

/* 心跳与超时（断线重连机制） */
#define HEARTBEAT_INTERVAL_MS   30000   // 客户端每 30s 发一次心跳
#define HEARTBEAT_TIMEOUT_MS    90000   // 服务端 90s 未收到任何数据判为掉线
#define RECONNECT_INTERVAL_MS   5000    // 客户端重连间隔（预留，客户端侧使用）

/* 充电计费：单价为电站属性（station 表 priceCents，单位：分/度），
 * 对站内全部电桩生效；本常量仅为新建电站时的默认电价（1.5 元/度 = 150 分） */
#define STATION_DEFAULT_PRICE       1.5     // 元/度（兼容保留）
#define STATION_DEFAULT_PRICE_CENTS 150     // 分/度（权威存储单位）

/* 订单待支付期限：停止充电后 15 分钟内未支付自动冻结用户 */
#define PAYMENT_DEADLINE_MS     (15 * 60 * 1000)

/* 预约充电业务规则：
 * 1) 预约保持时长最大 2 小时，超时由服务端自动注销（预约失效，押金不予退还）；
 * 2) 押金 20 元，预约成功即从钱包扣除（DEPOSIT 流水）；
 * 3) 预约用户正式开始充电时押金退还（REFUND 流水），预约记录转 Completed；
 * 4) 同一用户同时只能有一笔进行中预约，同一电桩同时只能被一人预约。 */
#define RESERVE_KEEP_MS         (2 * 60 * 60 * 1000)    // 预约保持时长：2 小时
#define RESERVE_DEPOSIT_CENTS   2000                    // 预约押金：20 元 = 2000 分

/* 服务器默认地址（客户端与服务器设置页均可配置覆盖）。
 * 客户端工程拷贝本文件后：把 SERVER_IP 改成服务器所在机器的局域网 IP 即可全工程生效；
 * 服务器端：settings 界面 IP 留空 = 绑定全部网卡（局域网测试必选，127.0.0.1 只能本机连）。 */
#define SERVER_IP   "10.194.99.223"
#define SERVER_PORT "12345"

/* 数据库表名（协议 msg 中 table 字段使用） */
#define TBL_USER        "user"              // 用户表（PC管理员 + 手机端用户，role 区分）
#define TBL_STATION     "station"           // 充电站表
#define TBL_CHARGER     "charger"           // 电桩表
#define TBL_ORDER       "orderInfo"         // 订单表（order 为 SQL 关键字，故用 orderInfo）
#define TBL_WALLET_TX   "walletTransaction" // 钱包资金流水表
#define TBL_RESERVATION "reservation"       // 预约表（预约业务已启用，见 §预约充电）

/* 电桩业务状态（charger 表 businessStatus 字段；与网络在线状态 online 分离） */
#define CHARGER_IDLE        0       // 空闲
#define CHARGER_RESERVED    1       // 已预约（预约充电业务）
#define CHARGER_CHARGING    2       // 充电中
#define CHARGER_FAULT       3       // 故障（预留）
#define CHARGER_RESTARTING  4       // 重启中（冗余预留）
/* 电桩网络在线状态（charger 表 online 字段）：0 离线 / 1 在线，与业务状态互不影响 */

/* 用户账户状态（user 表 status 字段） */
#define USER_STATUS_NORMAL  "Normal"    // 正常
#define USER_STATUS_FROZEN  "Frozen"    // 冻结：可登录/查询/充值/支付，不可开始新充电/新建预约

/* 用户角色（user 表 role 字段） */
#define ROLE_ADMIN      "admin" // PC 端普通管理员
#define ROLE_USER       "user"  // 手机端用户
#define ROLE_DEVICE     "device"// 电桩设备会话（仅协议交互，无账户身份）

/* 订单状态（orderInfo 表 status 字段） */
#define ORDER_CHARGING          "Charging"           // 充电中
#define ORDER_PENDING_SETTLE    "PendingSettlement"  // 待支付（15 分钟期限）
#define ORDER_SETTLED           "Settled"            // 已结算
#define ORDER_CANCELLED         "Cancelled"          // 已取消（冗余预留）

/* 钱包流水类型（walletTransaction 表 type 字段） */
#define TX_RECHARGE "RECHARGE"      // 充值
#define TX_PAY      "PAY"           // 订单支付
#define TX_REFUND   "REFUND"        // 退款（预约充电开始时退押金）
#define TX_DEPOSIT  "DEPOSIT"       // 预约押金（预约成功时扣）

/* 业务错误码（错误响应 JSON 的 code 字符串字段，网络层仍用 3xx 类型码） */
#define BIZ_ERR_FROZEN              "USER_FROZEN"            // 账户冻结，禁止开始新充电/预约
#define BIZ_ERR_INSUFFICIENT        "INSUFFICIENT_BALANCE"   // 余额不足
#define BIZ_ERR_CHARGER_UNAVAILABLE "CHARGER_UNAVAILABLE"    // 电桩不可用（不存在/离线/非空闲）
#define BIZ_ERR_STATE_CONFLICT      "STATE_CONFLICT"         // 状态冲突（如订单非待支付）
#define BIZ_ERR_NOT_FOUND           "NOT_FOUND"              // 目标不存在
#define BIZ_ERR_PARAM               "PARAM_ERROR"            // 参数缺失/非法
#define BIZ_ERR_DB                  "DB_ERROR"               // 数据库操作失败
#define BIZ_ERR_AUTH                "AUTH_FAIL"              // 认证失败（旧密码校验不过/越权修改他人资料）
#define BIZ_ERR_FORBIDDEN           "OP_FORBIDDEN"           // 业务规则禁止（删非空电站/改充电中桩等）
#define BIZ_ERR_DUPLICATE           "DUPLICATE"              // 唯一性冲突（用户名/手机号已占用）
#define BIZ_ERR_RESERVE_CONFLICT    "RESERVE_CONFLICT"       // 预约冲突（已有进行中预约/押金规则不满足）

/* ================ 消息类型定义 ================ */

/* 请求类型 1开头 */
#define GETDATA             100     // 通用查询 {table,cond} -> 200 {data:[...]}（user 表脱敏去 password）
#define LOGIN_REQ           101     // 登录 {username,password,role}（管理员 & 兼容）
#define LOGOUT_REQ          102     // 退出登录（v2.6.3：清除会话身份，连接保留，重登恢复）
#define START_CHARGING_REQ  108     // 开始充电（手机用户）{username, chargerCode}
#define STOP_CHARGING_REQ   109     // 停止充电 {chargerCode, username?}
#define CHGDATA_REQ         105     // 充电数据实时上报（电桩->服务器）{chargerCode, kwh, username?, stationName?}
#define ORDERQRY_REQ        106     // 订单查询 {username?,stationName?,chargerCode?,orderNo?,status?}
#define HEARTBEAT           107     // 心跳（msg 为空）
#define RECHARGE_REQ        113     // 钱包充值 {username, amount(元) 或 amountCents}
#define PAY_REQ             115     // 订单支付 {orderNo, username?}
#define PHONE_LOGIN_REQ     116     // 手机号免密登录（不存在自动注册）{phone}
#define ADDDATA             110     // 新增记录 {table, record}（高权限通用接口，保留）
#define UPDDATA             111     // 修改记录 {table, key, fields}（高权限通用接口，保留）
#define DELDATA             112     // 删除记录 {table, key}（高权限通用接口，保留）
#define DEV_ONLINE          120     // 设备上线（须已由管理员登记，未登记设备将被拒绝）{chargerCode,stationName?}
#define DEV_OFFLINE         121     // 设备主动下线 {chargerCode}
#define REG_REQ             117     // 用户注册（一般注册，免密登录之外的常规通道）{username,password,phone?,nickname?}
#define PROFILE_UPD_REQ     118     // 用户修改账户（用户名/密码/昵称/头像，同步改 user 表）{username,newUsername?,oldPassword?,newPassword?,nickname?,avatar?}
#define STATION_QRY_REQ     119     // 电站/电桩信息查询（用户端 & PC端）{stationName?,chargerCode?}
#define STATION_MNG_REQ     124     // 电站/电桩管理（PC管理员）{op:add|update|remove, target:station|charger, data?, key?}
#define RESERVE_REQ         125     // 预约充电（手机用户）{chargerCode}（押金 20 元即扣，保持 2 小时）
#define CANCEL_RESERVE_REQ  126     // 取消当前用户进行中的预约（载荷可为空）

/* 返回类型 2开头 */
#define DATA                200     // 通用查询结果（预留）
#define LOGIN_ACK           201     // 登录成功 {username,role}
#define LOGOUT_ACK          202     // 退出登录成功 {ok,username}（会话身份清除，连接保留）
#define START_CHARGING_ACK  208     // 开始充电成功 {orderNo, price, priceCents, startedAt}
#define STOP_CHARGING_ACK   209     // 停止充电成功 {orderNo, amount, amountCents, paymentDeadline}
#define ADDSUCCESS          210     // 新增成功
#define UPDSUCCESS          211     // 修改成功
#define DELSUCCESS          212     // 删除成功
#define CHGDATA_ACK         213     // 充电上报确认（实时累计）{orderNo, kwh, amount, amountCents, price, priceCents}
#define ORDERQRY_ACK        214     // 订单查询结果 {orders:[...]}
#define PAY_ACK             215     // 支付成功 {orderNo, balanceCents, balance}
#define RECHARGE_ACK        216     // 充值成功 {balanceCents, balance}
#define RESTART_REQ         114     // 远程重启电桩（管理员→服务端）{chargerCode}
#define RESTART_ACK         218     // 重启受理确认（服务端→管理员）{chargerCode, accepted}
#define PHONE_LOGIN_ACK     217     // 手机号登录成功 {username, status, balanceCents, balance, autoRegistered}
#define DEV_ONLINE_ACK      220     // 设备上线确认
#define DEV_OFFLINE_NOTICE  221     // 设备下线通知（服务端广播给管理员端）{chargerCode}
#define DEV_ONLINE_NOTICE   222     // 设备上线通知（服务端确认登记后广播给管理员端）{chargerCode}
#define PAYMENT_NOTICE      223     // 支付提醒（服务端→对应用户：订单进入待支付）{orderNo,amountCents,amount,paymentDeadline}
#define DEV_RESTART_NOTICE  224     // 远程重启通知（服务端→设备）{chargerCode}
#define CHG_PROGRESS        225     // 充电进度推送（服务端→对应用户）{orderNo,percent,kwh,amountCents,amount,remainMin}
#define CHG_FAULT_NOTICE    226     // 充电异常通知（服务端→对应用户）{chargerCode,orderNo,kwh,amountCents,amount,settled,reason}
#define CHARGER_FAULT_NOTICE 227    // 电桩故障广播（服务端→管理员）{chargerCode}
#define REG_ACK             219     // 注册成功 {ok,username,phone,nickname,autoLogin}
#define PROFILE_UPD_ACK     228     // 账户修改成功 {ok,username,changed:[...]}
#define STATION_QRY_ACK     229     // 电站/电桩查询结果 {stations:[{...station, chargers:[...]}]}
#define STATION_MNG_ACK     231     // 电站/电桩管理操作成功 {ok,op,target,...}
#define HEARTBEAT_ACK       230     // 心跳应答
#define RESERVE_ACK         232     // 预约成功 {reserveId,chargerCode,phone,reserveAt,expireAt,balanceCents,balance}
#define CANCEL_RESERVE_ACK  235     // 取消预约成功 {reserveId,chargerCode,refundCents,refund,balanceCents,balance}
#define RESERVE_EXPIRED_NOTICE 233  // 预约超时注销通知（服务端→用户）{reserveId,chargerCode,reserveAt,expireAt}
#define CHG_ORDER_NOTICE    234     // 订单状态同步（服务端→电桩设备会话）{chargerCode,status,orderNo?,username?}
                                    //   status: Reserved/Charging/Idle（电桩据此刷新本地展示）

/* 错误类型 3开头（错误响应 JSON 携带 code 字段 = 业务错误码，见 BIZ_ERR_*） */
#define DATA_NOEXIST    300     // 数据不存在
#define ILLEGAL_REQUEST 301     // 非法请求
#define LOGIN_FAIL      302     // 登录失败（用户名/密码错误）
#define DATA_EXIST      303     // 主键冲突（记录已存在）
#define DB_ERROR        304     // 数据库操作失败
#define PARAM_ERROR     305     // 参数缺失/非法
#define AUTH_ERROR      306     // 认证失败（修改密码时旧密码校验不过等）
#define OP_FORBIDDEN    307     // 业务规则禁止（删除非空电站、修改充电中电桩归属等）

/* 特殊类型 4开头 —— 大数据自动分片（对上层透明） */
#define BIGDATA_START   400     // 分片起始：payload = [4B原始类型码][数据块0]
#define BIGDATA_MID     401     // 分片中段：payload = 数据块n
#define BIGDATA_END     402     // 分片末段：payload = 数据块n(收尾)，重组后按原始类型码交付

#endif // PROTOCOL_H
