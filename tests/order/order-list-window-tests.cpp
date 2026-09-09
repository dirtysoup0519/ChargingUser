/* OrderListWindow 渲染测试：
 *  - message 非空且无订单时，空态文案显示 message 而不是"暂无订单"
 *  - message 非空且有订单时，顶部横幅可见并保留列表
 *  - messageIsError 决定横幅与空态的警示色调
 *  - 标题栏刷新按钮发出 refreshRequested
 */
#include "presentation/contracts/orderlistviewstate.h"
#include "presentation/pages/profile/orderlistwindow.h"

#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>

#include <QtTest>

class OrderListWindowTests final : public QObject
{
    Q_OBJECT
private slots:
    void emptyStateShowsMessageInsteadOfNoOrders();
    void messageWithOrdersShowsBanner();
    void messageIsErrorUsesErrorTone();
    void refreshButtonEmitsRefreshRequested();

private:
    QLabel *findLabel(OrderListWindow *window, const char *objectName) const
    {
        return window->findChild<QLabel *>(QLatin1String(objectName));
    }
};

void OrderListWindowTests::emptyStateShowsMessageInsteadOfNoOrders()
{
    OrderListWindow window;
    OrderListViewState state;
    state.message = QStringLiteral("订单加载失败，请重试");
    state.messageIsError = true;
    window.render(state);
    QCoreApplication::processEvents();
    QLabel *empty = findLabel(&window, "orderListEmpty");
    QVERIFY(empty);
    QVERIFY(empty->isVisible());
    QCOMPARE(empty->text(), state.message);
    // 无订单时横幅不重复展示
    QLabel *banner = findLabel(&window, "orderListBanner");
    QVERIFY(banner);
    QVERIFY(!banner->isVisible());
}

void OrderListWindowTests::messageWithOrdersShowsBanner()
{
    OrderListWindow window;
    OrderListViewState state;
    OrderListItemView order;
    order.businessId = QStringLiteral("od-1");
    order.type = OrderBusinessType::Charging;
    state.orders.append(order);
    state.message = QStringLiteral("正在刷新订单、预约和钱包流水…");
    window.render(state);
    QCoreApplication::processEvents();
    QLabel *banner = findLabel(&window, "orderListBanner");
    QVERIFY(banner);
    QVERIFY(banner->isVisible());
    QCOMPARE(banner->text(), state.message);
    QLabel *empty = findLabel(&window, "orderListEmpty");
    QVERIFY(empty);
    QVERIFY(!empty->isVisible());
}

void OrderListWindowTests::messageIsErrorUsesErrorTone()
{
    OrderListWindow window;
    OrderListViewState state;
    state.message = QStringLiteral("加载失败");
    state.messageIsError = true;
    window.render(state);
    QCoreApplication::processEvents();
    QLabel *empty = findLabel(&window, "orderListEmpty");
    QVERIFY(empty);
    QCOMPARE(empty->property("tone").toString(), QStringLiteral("error"));
    // 信息类消息不带警示色调
    OrderListViewState loading;
    loading.message = QStringLiteral("正在加载…");
    window.render(loading);
    QCoreApplication::processEvents();
    empty = findLabel(&window, "orderListEmpty");
    QVERIFY(empty);
    QVERIFY(empty->property("tone").toString().isEmpty());
}

void OrderListWindowTests::refreshButtonEmitsRefreshRequested()
{
    OrderListWindow window;
    QSignalSpy spy(&window, &OrderListWindow::refreshRequested);
    auto *refresh = window.findChild<QPushButton *>(QStringLiteral("refreshButton"));
    QVERIFY(refresh);
    QTest::mouseClick(refresh, Qt::LeftButton);
    QCOMPARE(spy.count(), 1);
}

QTEST_MAIN(OrderListWindowTests)
#include "order-list-window-tests.moc"
