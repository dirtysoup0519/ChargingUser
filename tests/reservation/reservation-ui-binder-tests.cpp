#include "app/reservationuibinder.h"
#include "modules/reservation/ireservationservice.h"

#include <QSignalSpy>
#include <QtTest>

class FakeReservationService final : public IReservationService
{
public:
    explicit FakeReservationService(QObject *parent = nullptr)
        : IReservationService(parent) {}

    RequestContext context;

public slots:
    void reserve(const RequestContext &requestContext,
                 const QString &, const QString &, int) override
    {
        context = requestContext;
    }
    void cancel(const QString &) override {}
    void cancelReservation(const RequestContext &) override {}
    void queryHistory(const RequestContext &) override {}
};

class ReservationUiBinderTests final : public QObject
{
    Q_OBJECT

private slots:
    void createdResultPreservesConfirmationDetails();
};

void ReservationUiBinderTests::createdResultPreservesConfirmationDetails()
{
    FakeReservationService service;
    ReservationUiBinder binder(&service);
    qRegisterMetaType<ReservationConfirmationViewState>();

    ReservationConfirmationViewState state;
    state.stationId = QStringLiteral("station-1");
    state.chargerId = QStringLiteral("charger-1");
    state.stationName = QStringLiteral("西区站");
    state.stationAddress = QStringLiteral("西区一号路");
    state.chargerCode = QStringLiteral("P101");
    state.chargerTypeText = QStringLiteral("快充");
    state.powerText = QStringLiteral("60 kW");
    state.status = ReservationConfirmationStatus::Ready;
    state.canReserve = true;
    binder.setConfirmationState(state);
    QCOMPARE(binder.currentState().stationName, QStringLiteral("西区站"));
    QSignalSpy stateChanged(&binder, &ReservationUiBinder::stateChanged);

    binder.reserveRequested(QStringLiteral("station-1"),
                            QStringLiteral("charger-1"), 7200);
    QVERIFY(!service.context.requestId.isEmpty());

    ReservationResult result;
    result.reservationId = QStringLiteral("r-1");
    result.chargerCode = QStringLiteral("P101");
    emit service.reservationCreated(service.context, result);

    QCOMPARE(stateChanged.count(), 2);
    const ReservationConfirmationViewState published =
        stateChanged.last().at(0).value<ReservationConfirmationViewState>();
    QCOMPARE(published.stationName, QStringLiteral("西区站"));
    QCOMPARE(published.stationAddress, QStringLiteral("西区一号路"));
    QCOMPARE(published.chargerTypeText, QStringLiteral("快充"));
    QCOMPARE(published.powerText, QStringLiteral("60 kW"));
    QCOMPARE(published.message, QStringLiteral("预约成功，编号：r-1"));
}

QTEST_GUILESS_MAIN(ReservationUiBinderTests)
#include "reservation-ui-binder-tests.moc"
