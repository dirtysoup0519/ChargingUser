#include "modules/charger/mockchargerservice.h"

#include <QTimer>

#include <algorithm>

namespace {

constexpr int kMaximumPageSize = 100;

bool isInside(const GeoPoint &point, const GeoBounds &bounds)
{
    return point.latitude >= bounds.southWest.latitude
           && point.latitude <= bounds.northEast.latitude
           && point.longitude >= bounds.southWest.longitude
           && point.longitude <= bounds.northEast.longitude;
}

} // namespace

MockChargerService::MockChargerService(QObject *parent)
    : IChargerService(parent)
{
}

void MockChargerService::setStationCatalog(const QVector<StationDetail> &stations)
{
    m_stationCatalog = stations;
}

void MockChargerService::setStationsBehavior(const Behavior &behavior)
{
    m_stationsBehavior = behavior;
}

void MockChargerService::setStationDetailBehavior(const Behavior &behavior)
{
    m_stationDetailBehavior = behavior;
}

int MockChargerService::stationsRequestCount() const
{
    return m_stationsRequestCount;
}

int MockChargerService::stationDetailRequestCount() const
{
    return m_stationDetailRequestCount;
}

QStringList MockChargerService::cancelledRequestIds() const
{
    return m_cancelledRequestIds;
}

void MockChargerService::queryStations(const RequestContext &context,
                                       const StationQuery &query)
{
    ++m_stationsRequestCount;
    if (!validateContext(context) || !validateStationQuery(context, query)) {
        return;
    }

    startRequest(context, m_stationsBehavior, [this, context, query] {
        emit stationsReady(context, buildPage(query));
    });
}

void MockChargerService::queryStationDetail(const RequestContext &context,
                                            const QString &stationId)
{
    ++m_stationDetailRequestCount;
    if (!validateContext(context)) {
        return;
    }
    if (stationId.trimmed().isEmpty()) {
        failLocal(context,
                  QStringLiteral("charger-invalid-station-id"),
                  QStringLiteral("Station ID is required."));
        return;
    }

    startRequest(context, m_stationDetailBehavior, [this, context, stationId] {
        const auto iterator = std::find_if(
            m_stationCatalog.cbegin(), m_stationCatalog.cend(),
            [&stationId](const StationDetail &detail) {
                return detail.stationId == stationId;
            });
        if (iterator == m_stationCatalog.cend()) {
            failLocal(context,
                      QStringLiteral("charger-station-not-found"),
                      QStringLiteral("Station details are unavailable."));
            return;
        }
        emit stationDetailReady(context, *iterator);
    });
}

void MockChargerService::cancel(const QString &requestId)
{
    if (requestId.isEmpty()) {
        return;
    }
    m_cancelledRequestIds.append(requestId);
    m_pendingRequests.remove(requestId);
}

bool MockChargerService::validateContext(const RequestContext &context)
{
    if (!context.isValid()) {
        failLocal(context,
                  QStringLiteral("charger-invalid-request"),
                  QStringLiteral("A request ID is required."));
        return false;
    }
    if (context.isMutation()) {
        failLocal(context,
                  QStringLiteral("charger-readonly-operation"),
                  QStringLiteral("Station queries must not contain an operation ID."));
        return false;
    }
    return true;
}

bool MockChargerService::validateStationQuery(const RequestContext &context,
                                              const StationQuery &query)
{
    if (!query.hasExactlyOneArea()
        || (query.center && !query.center->isValid())
        || (query.bounds && !query.bounds->isValid())) {
        failLocal(context,
                  QStringLiteral("charger-invalid-area"),
                  QStringLiteral("Exactly one valid search area is required."));
        return false;
    }
    if (query.pageSize < 1 || query.pageSize > kMaximumPageSize) {
        failLocal(context,
                  QStringLiteral("charger-invalid-page-size"),
                  QStringLiteral("Page size must be between 1 and 100."));
        return false;
    }
    if (!query.cursor.isEmpty()) {
        bool ok = false;
        const int offset = query.cursor.toInt(&ok);
        if (!ok || offset < 0) {
            failLocal(context,
                      QStringLiteral("charger-invalid-cursor"),
                      QStringLiteral("The station page cursor is invalid."));
            return false;
        }
    }
    return true;
}

void MockChargerService::startRequest(const RequestContext &context,
                                      const Behavior &behavior,
                                      Completion success)
{
    if (m_pendingRequests.contains(context.requestId)) {
        // 相同 requestId 不能拥有两个终态；发现重复时终结旧请求并拒绝新请求。
        m_pendingRequests.remove(context.requestId);
        failLocal(context,
                  QStringLiteral("charger-duplicate-request-id"),
                  QStringLiteral("The request ID is already in use."));
        return;
    }

    const quint64 serial = ++m_nextSerial;
    m_pendingRequests.insert(context.requestId, serial);

    if (behavior.timeoutMs >= 0) {
        QTimer::singleShot(behavior.timeoutMs, this, [this, context, serial] {
            finishFailure(context.requestId,
                          serial,
                          ClientError{},
                          QStringLiteral("charger-timeout"),
                          QStringLiteral("The station request timed out."),
                          true);
        });
    }

    if (behavior.outcome == Outcome::NoResponse) {
        return;
    }

    QTimer::singleShot(std::max(0, behavior.responseDelayMs), this,
                       [this, context, behavior, serial, success = std::move(success)] {
        if (behavior.outcome == Outcome::Failure) {
            finishFailure(context.requestId,
                          serial,
                          behavior.error,
                          QStringLiteral("charger-mock-failure"),
                          QStringLiteral("The station request failed."),
                          true);
            return;
        }
        if (!takePending(context.requestId, serial)) {
            return;
        }
        success();
    });
}

void MockChargerService::finishFailure(const QString &requestId,
                                       quint64 serial,
                                       const ClientError &configuredError,
                                       const QString &fallbackCode,
                                       const QString &fallbackMessage,
                                       bool fallbackRetryable)
{
    if (!takePending(requestId, serial)) {
        return;
    }
    ClientError error = configuredError;
    error.requestId = requestId;
    error.operationId.clear();
    error.resultUnknown = false;
    if (error.code.isEmpty()) {
        error.code = fallbackCode;
    }
    if (error.displayMessage.isEmpty()) {
        error.displayMessage = fallbackMessage;
    }
    if (configuredError.code.isEmpty()) {
        error.retryable = fallbackRetryable;
    }
    emit requestFailed(error);
}

bool MockChargerService::takePending(const QString &requestId, quint64 serial)
{
    const auto iterator = m_pendingRequests.find(requestId);
    if (iterator == m_pendingRequests.end() || iterator.value() != serial) {
        return false;
    }
    m_pendingRequests.erase(iterator);
    return true;
}

void MockChargerService::failLocal(const RequestContext &context,
                                   const QString &code,
                                   const QString &message,
                                   bool retryable)
{
    ClientError error;
    error.requestId = context.requestId;
    error.code = code;
    error.displayMessage = message;
    error.retryable = retryable;
    emit requestFailed(error);
}

StationPage MockChargerService::buildPage(const StationQuery &query) const
{
    QVector<StationSummary> matches;
    const QString keyword = query.keyword.trimmed();
    for (const StationDetail &detail : m_stationCatalog) {
        const StationSummary &station = detail.summary;
        if (!keyword.isEmpty()
            && !station.name.contains(keyword, Qt::CaseInsensitive)
            && !station.address.contains(keyword, Qt::CaseInsensitive)) {
            continue;
        }
        // 服务端可能返回缺少坐标但仍可查看文字详情的站点；只有已知坐标明确在
        // 视野外时才过滤，不能为缺坐标项制造位置或直接丢弃它。
        if (query.bounds && station.point
            && !isInside(*station.point, *query.bounds)) {
            continue;
        }
        matches.append(station);
    }

    const int offset = query.cursor.isEmpty() ? 0 : query.cursor.toInt();
    const int remaining = std::max(0, static_cast<int>(matches.size()) - offset);
    const int count = std::min(query.pageSize, remaining);

    StationPage page;
    page.items.reserve(count);
    for (int index = 0; index < count; ++index) {
        page.items.append(matches.at(offset + index));
    }
    const int nextOffset = offset + count;
    page.hasMore = nextOffset < matches.size();
    if (page.hasMore) {
        page.nextCursor = QString::number(nextOffset);
    }
    return page;
}
