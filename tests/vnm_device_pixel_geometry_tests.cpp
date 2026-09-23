#include "vnm_qml_chrome/vnm_qml_chrome_runtime.h"
#include "vnm_qml_chrome/vnm_chrome_geometry.h"
#include "vnm_device_pixel_geometry_item.h"

#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QDebug>
#include <QMetaObject>
#include <QObject>
#include <QPointF>
#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtTest/QTest>
#include <QVariant>

#include <cmath>
#include <limits>
#include <memory>

namespace
{

QString component_error_string(const QQmlComponent& component)
{
    QStringList messages;
    for (const QQmlError& error : component.errors()) {
        messages.push_back(error.toString());
    }
    return messages.join(QStringLiteral("\n"));
}

std::unique_ptr<QObject> create_qml_root(
    QQmlEngine& engine,
    const char* source,
    const QString& path)
{
    QQmlComponent component(&engine);
    component.setData(source, QUrl(path));
    if (!component.isReady()) {
        qWarning().noquote() << component_error_string(component);
        return nullptr;
    }

    std::unique_ptr<QObject> root(component.create());
    if (!root) {
        qWarning().noquote() << component_error_string(component);
    }
    return root;
}

QQuickItem* find_item(QObject* root, const QString& object_name)
{
    return root->findChild<QQuickItem*>(object_name);
}

QQuickItem* painted_leaf(QQuickItem* root)
{
    return root->findChild<QQuickItem*>(QStringLiteral("painted_shape"));
}

QQuickItem* geometry_backend(QQuickItem* root)
{
    if (auto* backend = qobject_cast<VNM_Device_pixel_geometry_item*>(root)) {
        return backend;
    }

    for (QQuickItem* child : root->childItems()) {
        if (QQuickItem* backend = geometry_backend(child)) {
            return backend;
        }
    }
    return nullptr;
}

bool on_device_grid(qreal scene_coordinate, qreal device_pixel_ratio)
{
    // This checks the nominal DPR arithmetic, not rendered viewport pixels.
    const qreal physical_coordinate = scene_coordinate * device_pixel_ratio;
    return std::abs(physical_coordinate - std::round(physical_coordinate)) < 0.0001;
}

QString edge_diagnostic(
    const char* edge_name,
    qreal scene_coordinate,
    qreal device_pixel_ratio)
{
    return QStringLiteral("%1 scene=%2 nominal_physical=%3 dpr=%4")
        .arg(QString::fromUtf8(edge_name))
        .arg(scene_coordinate, 0, 'f', 6)
        .arg(scene_coordinate * device_pixel_ratio, 0, 'f', 6)
        .arg(device_pixel_ratio, 0, 'f', 6);
}

} // namespace

class Vnm_device_pixel_geometry_tests : public QObject
{
    Q_OBJECT

private slots:
    void snapped_rectangle_survives_scene_graph_recreation_without_hover()
    {
        if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
            QSKIP("Scene-graph discard requires the native-window recovery test registration.");
        }
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));
        auto root = create_qml_root(engine, R"QML(
import QtQuick
import QtQuick.Window
import VNM_Chrome
Window {
    width: 160; height: 100; color: "black"
    VNM_Snapped_rectangle {
        objectName: "surface"
        x: 20; y: 20; width: 100; height: 50
        color: "red"; border_color: "white"; border_width: 1
    }
}
)QML", QStringLiteral("qrc:/tests/shape_resource_recovery.qml"));
        QVERIFY(root);
        auto* window = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(window);
        window->setPersistentGraphics(false);
        window->setPersistentSceneGraph(false);
        QSignalSpy invalidated(window, &QQuickWindow::sceneGraphInvalidated);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        QImage initial;
        QTRY_VERIFY(!(initial = window->grabWindow()).isNull());
        const qreal dpr = initial.devicePixelRatio();
        QCOMPARE(initial.pixelColor(qRound(60 * dpr), qRound(40 * dpr)), QColor("red"));
        auto white_pixels = [](const QImage& image) {
            int count = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    const QColor pixel = image.pixelColor(x, y);
                    if (pixel.red() > 180 && pixel.green() > 180 && pixel.blue() > 180) {
                        ++count;
                    }
                }
            }
            return count;
        };
        QVERIFY(white_pixels(initial) > 0);
        for (int cycle = 1; cycle <= 2; ++cycle) {
            window->hide();
            window->releaseResources();
            QTRY_COMPARE(invalidated.count(), cycle);
            window->show();
            QVERIFY(QTest::qWaitForWindowExposed(window));
            QImage restored;
            QTRY_VERIFY((restored = window->grabWindow()) == initial);
            QCOMPARE(white_pixels(restored), white_pixels(initial));
        }
        auto* surface = find_item(root.get(), QStringLiteral("surface"));
        QVERIFY(surface);
        QVERIFY(surface->setProperty("border_width", 2));
        QTRY_VERIFY(white_pixels(window->grabWindow()) > white_pixels(initial));
    }

    void arithmetic_snaps_edges_independently()
    {
        const QRectF logical_rect(10.3, 4.7, 231.4, 37.9);
        const QRectF snapped = vnm_qml_chrome::snapped_logical_rect(
            logical_rect,
            1.25);

        QCOMPARE(snapped.left(), 10.4);
        QCOMPARE(snapped.top(), 4.8);
        QCOMPARE(snapped.right(), 241.6);
        QCOMPARE(snapped.bottom(), 42.4);
        QCOMPARE(
            vnm_qml_chrome::normalized_device_pixel_ratio(
                std::numeric_limits<qreal>::quiet_NaN()),
            1.0);
        QCOMPARE(vnm_qml_chrome::normalized_device_pixel_ratio(0.75), 0.75);
    }


    void process_scale_factor_reaches_window()
    {
        const QByteArray configured_scale = qgetenv("QT_SCALE_FACTOR");
        QVERIFY2(!configured_scale.isEmpty(), "QT_SCALE_FACTOR must be set by CTest.");

        bool parsed = false;
        const qreal expected_dpr = configured_scale.toDouble(&parsed);
        QVERIFY2(parsed && expected_dpr > 0.0, "QT_SCALE_FACTOR must be a positive number.");

        QQuickWindow window;
        window.resize(320, 180);
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(), 2000);

        QCOMPARE(window.effectiveDevicePixelRatio(), expected_dpr);

        const QImage image = window.grabWindow();
        QVERIFY(!image.isNull());
        QCOMPARE(image.size(), QSize(
            qRound(window.width() * expected_dpr),
            qRound(window.height() * expected_dpr)));
        QCOMPARE(
            qreal(image.width()) / qreal(window.width()),
            expected_dpr);
        QCOMPARE(
            qreal(image.height()) / qreal(window.height()),
            expected_dpr);
    }

    void translation_preserves_rounded_span_data()
    {
        QTest::addColumn<qreal>("scale");
        QTest::addColumn<qreal>("logical_extent");
        QTest::addColumn<int>("physical_extent");

        // These exact binary inputs put the scaled span at 12.5 or 37.5 pixels.
        // Nearest-integer rounding sends the tie upward; its adjacent
        // representable input below the tie must still round downward.
        QTest::newRow("half_pixel")        << 1.0   << 30.0 << 38;
        QTest::newRow("scaled_half_pixel") << 1.25  << 8.0  << 13;
        QTest::newRow("uniform_scale_span") << 1.875 << 16.0 << 38;
        QTest::newRow("below_half_pixel")
            << 1.0 << std::nextafter(30.0, 0.0) << 37;
        QTest::newRow("above_half_pixel")
            << 1.0 << std::nextafter(30.0, 31.0) << 38;
    }

    void translation_preserves_rounded_span()
    {
        QFETCH(qreal, scale);
        QFETCH(qreal, logical_extent);
        QFETCH(int, physical_extent);

        QQuickWindow window;
        const qreal dpr = window.effectiveDevicePixelRatio();
        QCOMPARE(dpr, 1.25);

        QQuickItem ancestor(window.contentItem());
        ancestor.setScale(scale);
        QQuickItem host(&ancestor);
        host.setX(3.3);
        host.setY(4.1);
        host.setWidth(logical_extent);
        host.setHeight(logical_extent);
        VNM_Device_pixel_geometry_item geometry(&host);
        QCoreApplication::processEvents();

        const qreal expected_extent = physical_extent / (scale * dpr);
        QCOMPARE(geometry.width(), expected_extent);
        QCOMPARE(geometry.height(), expected_extent);
        QSignalSpy width_changes(&geometry, &QQuickItem::widthChanged);
        QSignalSpy height_changes(&geometry, &QQuickItem::heightChanged);

        // Large translations also expose cancellation in scale reconstruction.
        for (qreal position : {-1048576.3, -31.9, 0.1, 10.3, 17.7, 1048576.7}) {
            ancestor.setX(position);
            ancestor.setY(-position);
            QCoreApplication::processEvents();
            QCOMPARE(geometry.width(), expected_extent);
            QCOMPARE(geometry.height(), expected_extent);
            QCOMPARE(host.width(), logical_extent);
            QCOMPARE(host.height(), logical_extent);
            const QPointF near = geometry.mapToScene(QPointF(0.0, 0.0));
            const QPointF far = geometry.mapToScene(
                QPointF(geometry.width(), geometry.height()));
            QVERIFY(on_device_grid(near.x(), dpr));
            QVERIFY(on_device_grid(near.y(), dpr));
            QVERIFY(on_device_grid(far.x(), dpr));
            QVERIFY(on_device_grid(far.y(), dpr));
        }
        QCOMPARE(width_changes.count(), 0);
        QCOMPARE(height_changes.count(), 0);
    }

    void independently_rounded_span_bounds_edge_displacement()
    {
        QQuickWindow window;
        const qreal dpr = window.effectiveDevicePixelRatio();
        QQuickItem host(window.contentItem());
        VNM_Device_pixel_geometry_item geometry(&host);

        // Near-edge and span errors can have the same sign. Their sum
        // approaches one pixel at the far edge, although each is at most half.
        for (qreal phase : {0.01, 0.49, 0.5, 0.51, 0.99}) {
            host.setWidth((37.0 + phase) / dpr);
            host.setHeight((37.0 + phase) / dpr);
            for (qreal origin : {-0.99, -0.51, -0.5, -0.49, 0.49, 0.5, 0.51, 0.99}) {
                host.setX(origin / dpr);
                host.setY(origin / dpr);
                QCoreApplication::processEvents();
                const QPointF logical_near = host.mapToScene(QPointF(0.0, 0.0));
                const QPointF logical_far = host.mapToScene(
                    QPointF(host.width(), host.height()));
                const QPointF painted_near = geometry.mapToScene(QPointF(0.0, 0.0));
                const QPointF painted_far = geometry.mapToScene(
                    QPointF(geometry.width(), geometry.height()));
                const QPointF near_error = (painted_near - logical_near) * dpr;
                const QPointF far_error = (painted_far - logical_far) * dpr;
                QVERIFY(std::abs(near_error.x()) <= 0.5 + 1.0e-12);
                QVERIFY(std::abs(near_error.y()) <= 0.5 + 1.0e-12);
                QVERIFY(std::abs(far_error.x()) <= 1.0 + 1.0e-12);
                QVERIFY(std::abs(far_error.y()) <= 1.0 + 1.0e-12);
            }
        }
    }

    void rectangle_resnaps_after_ancestor_move()
    {
        exercise_rectangle_scene_change("ancestor", 0.37);
    }

    void rectangle_resnaps_after_quick_translate()
    {
        exercise_rectangle_scene_change("scene_translation", 0.37);
    }

    void corrected_geometry_survives_uniform_scale_and_reparent()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"QML(
import QtQuick
import QtQuick.Window
import VNM_Chrome

Window {
    visible: true
    width: 400
    height: 200

    Item {
        id: first_parent
        objectName: "first_parent"
        x: 7.3
        y: 4.1
        scale: 1.5

        VNM_Snapped_rectangle {
            objectName: "snapped_host"
            x: 2.2
            y: 1.4
            width: 51.7
            height: 22.3
            color: "red"
        }
    }

    Item {
        id: second_parent
        objectName: "second_parent"
        x: 103.7
        y: 61.9
        scale: 1.25
    }
}
)QML";

        std::unique_ptr<QObject> root = create_qml_root(
            engine,
            qml_source,
            QStringLiteral("qrc:/tests/snapped_geometry_reparent.qml"));
        QVERIFY(root != nullptr);

        auto* window = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(window != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(window->isExposed(), 2000);

        QQuickItem* host = find_item(root.get(), QStringLiteral("snapped_host"));
        QQuickItem* second_parent = find_item(
            root.get(),
            QStringLiteral("second_parent"));
        QVERIFY(host != nullptr);
        QVERIFY(second_parent != nullptr);

        verify_painted_edges(host, window->effectiveDevicePixelRatio());
        host->setParentItem(second_parent);
        QCoreApplication::processEvents();
        verify_painted_edges(host, window->effectiveDevicePixelRatio());
    }

    void unsupported_transform_falls_back_and_recovers()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"QML(
import QtQuick
import QtQuick.Window
import VNM_Chrome

Window {
    visible: true
    width: 400
    height: 200

    Item {
        id: transform_owner
        objectName: "transform_owner"
        x: 10.3
        y: 4.7
        transform: Scale {
            id: non_uniform_scale
            objectName: "non_uniform_scale"
        }

        VNM_Snapped_rectangle {
            objectName: "snapped_host"
            width: 71.6
            height: 32.4
            color: "red"
        }
    }
}
)QML";

        std::unique_ptr<QObject> root = create_qml_root(
            engine,
            qml_source,
            QStringLiteral("qrc:/tests/snapped_geometry_fallback.qml"));
        QVERIFY(root != nullptr);

        auto* window = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(window != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(window->isExposed(), 2000);

        QQuickItem* owner = find_item(root.get(), QStringLiteral("transform_owner"));
        QQuickItem* host = find_item(root.get(), QStringLiteral("snapped_host"));
        QVERIFY(owner != nullptr);
        QVERIFY(host != nullptr);
        QQuickItem* backend = geometry_backend(host);
        QVERIFY(backend != nullptr);
        QVERIFY(backend->property("snapping_active").toBool());

        owner->setRotation(13.0);
        QCoreApplication::processEvents();
        QVERIFY(!backend->property("snapping_active").toBool());
        QCOMPARE(backend->x(), 0.0);
        QCOMPARE(backend->y(), 0.0);
        QCOMPARE(backend->width(), host->width());
        QCOMPARE(backend->height(), host->height());

        QObject* non_uniform_scale = root->findChild<QObject*>(
            QStringLiteral("non_uniform_scale"));
        QVERIFY(non_uniform_scale != nullptr);
        owner->setRotation(0.0);
        non_uniform_scale->setProperty("xScale", 1.25);
        non_uniform_scale->setProperty("yScale", 0.75);
        QCoreApplication::processEvents();
        QVERIFY(!backend->property("snapping_active").toBool());
        QCOMPARE(backend->x(), 0.0);
        QCOMPARE(backend->y(), 0.0);
        QCOMPARE(backend->width(), host->width());
        QCOMPARE(backend->height(), host->height());

        non_uniform_scale->setProperty("xScale", 1.0);
        non_uniform_scale->setProperty("yScale", 1.0);
        QCoreApplication::processEvents();
        QVERIFY(backend->property("snapping_active").toBool());
        verify_painted_edges(host, window->effectiveDevicePixelRatio());
    }

    void snapped_rectangle_gradient_selection_preserves_solid_fill_and_stroke()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"QML(
import QtQuick
import QtQuick.Shapes
import VNM_Chrome

VNM_Snapped_rectangle {
    width: 26
    height: 26
    color: "#202020"
    border_color: "#808080"
    border_width: 1
    property bool use_gradient: true
    fill_gradient: use_gradient ? accent_gradient : null

    RadialGradient {
        id: accent_gradient
        objectName: "accent_gradient"
        centerX: 13
        centerY: 13
        centerRadius: 18
        focalX: centerX
        focalY: centerY
        GradientStop { position: 0; color: "#202020" }
        GradientStop { position: 1; color: "#802020" }
    }
}
)QML";

        std::unique_ptr<QObject> root = create_qml_root(
            engine,
            qml_source,
            QStringLiteral("qrc:/tests/snapped_rectangle_gradient.qml"));
        QVERIFY(root != nullptr);
        QObject* path = root->findChild<QObject*>(QStringLiteral("border_shape_path"));
        QObject* gradient = root->findChild<QObject*>(QStringLiteral("accent_gradient"));
        QVERIFY(path != nullptr);
        QVERIFY(gradient != nullptr);
        QCOMPARE(path->property("fillGradient").value<QObject*>(), gradient);
        QCOMPARE(path->property("fillColor").value<QColor>(), QColor("#202020"));
        QCOMPARE(path->property("strokeColor").value<QColor>(), QColor("#808080"));
        QCOMPARE(path->property("strokeWidth").toReal(), 1.0);

        QVERIFY(root->setProperty("use_gradient", false));
        QVERIFY(path->property("fillGradient").value<QObject*>() == nullptr);
        QCOMPARE(path->property("fillColor").value<QColor>(), QColor("#202020"));
        QCOMPARE(path->property("strokeColor").value<QColor>(), QColor("#808080"));
        QCOMPARE(path->property("strokeWidth").toReal(), 1.0);

        QVERIFY(root->setProperty("use_gradient", true));
        QCOMPARE(path->property("fillGradient").value<QObject*>(), gradient);
    }

    void snapped_rectangle_forwards_independent_corner_radii()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"QML(
import QtQuick
import VNM_Chrome

VNM_Snapped_rectangle {
    objectName: "snapped_rectangle"
    width: 80
    height: 40
    radius: 9
    topLeftRadius: 1
    topRightRadius: 2
    bottomLeftRadius: 3
    bottomRightRadius: 4
}
)QML";

        std::unique_ptr<QObject> root = create_qml_root(
            engine,
            qml_source,
            QStringLiteral("qrc:/tests/snapped_rectangle_radii.qml"));
        QVERIFY(root != nullptr);

        auto* host = qobject_cast<QQuickItem*>(root.get());
        QVERIFY(host != nullptr);
        QObject* border_path = host->findChild<QObject*>(
            QStringLiteral("border_path_rectangle"));
        QVERIFY(border_path != nullptr);
        QCOMPARE(border_path->property("radius").toReal(), 9.0);
        QCOMPARE(border_path->property("topLeftRadius").toReal(), 1.0);
        QCOMPARE(border_path->property("topRightRadius").toReal(), 2.0);
        QCOMPARE(border_path->property("bottomLeftRadius").toReal(), 3.0);
        QCOMPARE(border_path->property("bottomRightRadius").toReal(), 4.0);

        QVERIFY(host->setProperty("topLeftRadius", 5.0));
        QCOMPARE(border_path->property("topLeftRadius").toReal(), 5.0);
    }


private:
    void verify_painted_edges(QQuickItem* host, qreal dpr)
    {
        QQuickItem* paint = painted_leaf(host);
        QVERIFY(paint != nullptr);
        const QPointF origin = paint->mapToScene(QPointF(0.0, 0.0));
        const QPointF far = paint->mapToScene(
            QPointF(paint->width(), paint->height()));
        QVERIFY2(on_device_grid(origin.x(), dpr),
            qPrintable(edge_diagnostic("left", origin.x(), dpr)));
        QVERIFY2(on_device_grid(origin.y(), dpr),
            qPrintable(edge_diagnostic("top", origin.y(), dpr)));
        QVERIFY2(on_device_grid(far.x(), dpr),
            qPrintable(edge_diagnostic("right", far.x(), dpr)));
        QVERIFY2(on_device_grid(far.y(), dpr),
            qPrintable(edge_diagnostic("bottom", far.y(), dpr)));
    }

    void exercise_rectangle_scene_change(const char* property_name, qreal device_delta)
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"QML(
import QtQuick
import QtQuick.Window
import VNM_Chrome

Window {
    id: window
    visible: true
    width: 400
    height: 200

    property alias ancestor: ancestor
    property alias scene_translation: scene_translation

    Item {
        id: ancestor
        x: 10.3
        y: 4.7
        transform: Translate {
            id: scene_translation
        }

        VNM_Snapped_rectangle {
            objectName: "rectangle_host"
            width: 71.6
            height: 120
        }
    }
}
)QML";

        std::unique_ptr<QObject> root = create_qml_root(
            engine,
            qml_source,
            QStringLiteral("qrc:/tests/rectangle_device_pixel_geometry.qml"));
        QVERIFY(root != nullptr);

        auto* window = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(window != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(window->isExposed(), 2000);

        QQuickItem* rectangle_host = find_item(
            root.get(),
            QStringLiteral("rectangle_host"));
        QVERIFY(rectangle_host != nullptr);
        QQuickItem* paint = painted_leaf(rectangle_host);
        QVERIFY(paint != nullptr);

        const qreal dpr = window->effectiveDevicePixelRatio();
        const QPointF before = paint->mapToScene(QPointF(0.0, 0.0));
        QVERIFY2(on_device_grid(before.x(), dpr),
            qPrintable(edge_diagnostic("initial left", before.x(), dpr)));

        QObject* movement_owner = root->property(property_name).value<QObject*>();
        QVERIFY(movement_owner != nullptr);
        const qreal logical_delta = device_delta / dpr;
        movement_owner->setProperty(
            "x",
            movement_owner->property("x").toReal() + logical_delta);
        QCoreApplication::processEvents();

        paint = painted_leaf(rectangle_host);
        const QPointF after = paint->mapToScene(QPointF(0.0, 0.0));
        QVERIFY2(on_device_grid(after.x(), dpr),
            qPrintable(edge_diagnostic("moved left", after.x(), dpr)));
    }
};

QTEST_MAIN(Vnm_device_pixel_geometry_tests)

#include "vnm_device_pixel_geometry_tests.moc"
