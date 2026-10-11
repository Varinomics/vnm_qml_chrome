#include "vnm_qml_chrome/vnm_system_window.h"

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPointF>
#include <QRect>
#include <QWindow>
#include <QtTest/QTest>

#include <algorithm>
#include <memory>

namespace {

class Resize_window : public QWindow
{
public:
    vnm_qml_chrome::System_window* system_window = nullptr;
    Qt::Edges                     resize_edges;
    bool                          resize_started = false;

protected:
    bool event(QEvent* event) override
    {
        if (system_window && event->type() == QEvent::MouseButtonPress) {
            resize_started = system_window->start_system_resize(this, resize_edges.toInt());
        }
        return QWindow::event(event);
    }
};

void configure_window(Resize_window& window, vnm_qml_chrome::System_window& service, Qt::Edges edges)
{
    window.system_window = &service;
    window.resize_edges  = edges;
    window.setFlags(Qt::Window | Qt::FramelessWindowHint);
    window.setGeometry(250, 200, 400, 240);
    window.setMinimumSize(QSize(200, 120));
    window.setMaximumSize(QSize(600, 360));
    window.show();
}

QPoint press_position(const Resize_window& window)
{
    QPoint position(window.width() / 2, window.height() / 2);
    if (window.resize_edges.testFlag(Qt::LeftEdge))   { position.setX(1);                   }
    if (window.resize_edges.testFlag(Qt::RightEdge))  { position.setX(window.width() - 2);  }
    if (window.resize_edges.testFlag(Qt::TopEdge))    { position.setY(1);                   }
    if (window.resize_edges.testFlag(Qt::BottomEdge)) { position.setY(window.height() - 2); }
    return position;
}

void move_to(QWindow& window, const QPointF& global_position)
{
    QMouseEvent move(QEvent::MouseMove,
        window.mapFromGlobal(global_position.toPoint()), global_position,
        Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &move);
}

QRect expected_geometry(const QRect& start, Qt::Edges edges, const QPoint& delta)
{
    QRect result = start;
    if (edges.testFlag(Qt::LeftEdge)) {
        result.setLeft(start.x() + start.width() - std::clamp(start.width() - delta.x(), 200, 600));
    }
    if (edges.testFlag(Qt::RightEdge)) {
        result.setWidth(std::clamp(start.width() + delta.x(), 200, 600));
    }
    if (edges.testFlag(Qt::TopEdge)) {
        result.setTop(start.y() + start.height() - std::clamp(start.height() - delta.y(), 120, 360));
    }
    if (edges.testFlag(Qt::BottomEdge)) {
        result.setHeight(std::clamp(start.height() + delta.y(), 120, 360));
    }
    return result;
}

void add_edge_rows()
{
    QTest::addColumn<int>("edges");
    QTest::newRow("left")         << int(Qt::LeftEdge);
    QTest::newRow("right")        << int(Qt::RightEdge);
    QTest::newRow("top")          << int(Qt::TopEdge);
    QTest::newRow("bottom")       << int(Qt::BottomEdge);
    QTest::newRow("top-left")     << int(Qt::TopEdge    | Qt::LeftEdge);
    QTest::newRow("top-right")    << int(Qt::TopEdge    | Qt::RightEdge);
    QTest::newRow("bottom-left")  << int(Qt::BottomEdge | Qt::LeftEdge);
    QTest::newRow("bottom-right") << int(Qt::BottomEdge | Qt::RightEdge);
}

} // namespace

class Macos_window_resize_tests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QCOMPARE(QGuiApplication::platformName(), QStringLiteral("cocoa"));
    }

    void edges_and_corners_data() { add_edge_rows(); }

    void edges_and_corners()
    {
        QFETCH(int, edges);
        vnm_qml_chrome::System_window service;
        Resize_window window;
        configure_window(window, service, Qt::Edges::fromInt(edges));
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const QRect start = window.geometry();
        const QPoint press = press_position(window);
        const QPointF global_press = window.mapToGlobal(press);
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, press);
        QVERIFY(window.resize_started);
        const QPoint delta(25, 18);
        move_to(window, global_press + delta);
        QTRY_COMPARE(window.geometry(), expected_geometry(start, window.resize_edges, delta));
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier,
            window.mapFromGlobal((global_press + delta).toPoint()));
        const QRect finished = window.geometry();
        move_to(window, global_press + QPoint(80, 70));
        QCOMPARE(window.geometry(), finished);
    }

    void minimum_and_maximum_sizes_data() { add_edge_rows(); }

    void minimum_and_maximum_sizes()
    {
        QFETCH(int, edges);
        for (const QPoint delta : {QPoint(1000, 1000), QPoint(-1000, -1000)}) {
            vnm_qml_chrome::System_window service;
            Resize_window window;
            configure_window(window, service, Qt::Edges::fromInt(edges));
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            const QRect start = window.geometry();
            const QPoint press = press_position(window);
            const QPointF global_press = window.mapToGlobal(press);
            QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, press);
            QVERIFY(window.resize_started);
            move_to(window, global_press + delta);
            QTRY_COMPARE(window.geometry(), expected_geometry(start, window.resize_edges, delta));
            QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier,
                window.mapFromGlobal((global_press + delta).toPoint()));
        }
    }

    void cancellation_data()
    {
        QTest::addColumn<int>("event_type");
        QTest::newRow("grab-loss")      << int(QEvent::UngrabMouse);
        QTest::newRow("deactivation")   << int(QEvent::WindowDeactivate);
        QTest::newRow("hidden")         << int(QEvent::Hide);
        QTest::newRow("state-change")   << int(QEvent::WindowStateChange);
        QTest::newRow("closed")         << int(QEvent::Close);
    }

    void cancellation()
    {
        QFETCH(int, event_type);
        vnm_qml_chrome::System_window service;
        Resize_window window;
        configure_window(window, service, Qt::RightEdge);
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const QPoint press = press_position(window);
        const QPointF global_press = window.mapToGlobal(press);
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, press);
        QVERIFY(window.resize_started);
        move_to(window, global_press + QPoint(30, 0));
        if (event_type == QEvent::WindowStateChange) {
            QWindowStateChangeEvent state_change(Qt::WindowNoState);
            QCoreApplication::sendEvent(&window, &state_change);
        }
        else {
            QEvent cancellation{QEvent::Type(event_type)};
            QCoreApplication::sendEvent(&window, &cancellation);
        }
        const QRect finished = window.geometry();
        move_to(window, global_press + QPoint(90, 0));
        QCOMPARE(window.geometry(), finished);
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier,
            window.mapFromGlobal(global_press.toPoint()));
    }

    void requires_press_from_the_target_window()
    {
        vnm_qml_chrome::System_window service;
        Resize_window window;
        Resize_window other;
        configure_window(window, service, Qt::RightEdge);
        configure_window(other, service, Qt::RightEdge);
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(!service.start_system_resize(&window, int(Qt::RightEdge)));
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, press_position(window));
        QVERIFY(window.resize_started);
        QVERIFY(!service.start_system_resize(&other, int(Qt::RightEdge)));
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, press_position(window));
    }

    void destroyed_window_ends_the_gesture()
    {
        vnm_qml_chrome::System_window service;
        auto window = std::make_unique<Resize_window>();
        configure_window(*window, service, Qt::RightEdge);
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        QTest::mousePress(window.get(), Qt::LeftButton, Qt::NoModifier, press_position(*window));
        QVERIFY(window->resize_started);
        window.reset();
        Resize_window next;
        configure_window(next, service, Qt::RightEdge);
        QVERIFY(QTest::qWaitForWindowExposed(&next));
        const QRect unchanged = next.geometry();
        move_to(next, next.mapToGlobal(QPoint(500, 120)));
        QCOMPARE(next.geometry(), unchanged);
        QVERIFY(!service.start_system_resize(&next, int(Qt::RightEdge)));
        QTest::mouseRelease(&next, Qt::LeftButton);
    }
};

QTEST_MAIN(Macos_window_resize_tests)
#include "vnm_macos_window_resize_tests.moc"
