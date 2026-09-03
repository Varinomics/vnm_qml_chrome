#include "vnm_qml_chrome/vnm_qml_chrome_runtime.h"

#include "vnm_qml_chrome/vnm_chrome_geometry.h"
#include "vnm_qml_chrome/vnm_native_window_frame.h"
#include "vnm_qml_chrome/vnm_system_window.h"

#include <QClipboard>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QPointF>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QResource>
#include <QSignalSpy>
#include <QStyleHints>
#include <QString>
#include <QXmlStreamReader>
#include <QtTest/QTest>

#include <cmath>
#include <limits>
#include <memory>

namespace {

QString component_error_string(const QQmlComponent& component)
{
    QStringList out;
    const auto errors = component.errors();
    for (const QQmlError& error : errors) {
        out.push_back(error.toString());
    }
    return out.join(QStringLiteral("\n"));
}

void collect_descendants(QObject* object, QList<QObject*>& out)
{
    const auto object_children = object->children();
    for (QObject* child : object_children) {
        if (!out.contains(child)) {
            out.push_back(child);
            collect_descendants(child, out);
        }
    }

    auto* item = qobject_cast<QQuickItem*>(object);
    if (!item) {
        return;
    }

    const auto child_items = item->childItems();
    for (QQuickItem* child : child_items) {
        if (!out.contains(child)) {
            out.push_back(child);
            collect_descendants(child, out);
        }
    }
}

QObject* find_descendant(QObject* root, const QString& object_name)
{
    if (root->objectName() == object_name) {
        return root;
    }

    QList<QObject*> descendants;
    collect_descendants(root, descendants);
    for (QObject* object : descendants) {
        if (object->objectName() == object_name) {
            return object;
        }
    }
    return nullptr;
}

QQuickItem* find_item(QObject* root, const QString& object_name)
{
    return qobject_cast<QQuickItem*>(find_descendant(root, object_name));
}

std::unique_ptr<QObject> create_qml_object(
    QQmlEngine& engine,
    const char* qml_source,
    const char* resource_path)
{
    QQmlComponent component(&engine);
    component.setData(qml_source, QUrl(QString::fromUtf8(resource_path)));
    if (!component.isReady()) {
        qWarning().noquote() << component_error_string(component);
        return nullptr;
    }

    return std::unique_ptr<QObject>(component.create());
}

bool has_property(QObject* object, const char* property_name)
{
    return object->property(property_name).isValid();
}

bool has_signal(QObject* object, const char* signal_signature)
{
    const QByteArray normalized = QMetaObject::normalizedSignature(signal_signature);
    return object->metaObject()->indexOfSignal(normalized.constData()) >= 0;
}

QColor object_color(QObject* object, const char* property_name)
{
    return object->property(property_name).value<QColor>();
}

bool nearly_equal(qreal actual, qreal expected)
{
    return std::abs(actual - expected) <= 0.000001;
}

bool rect_nearly_equal(
    const QRectF& actual,
    const QRectF& expected)
{
    return
        nearly_equal(actual.x(),      expected.x())      &&
        nearly_equal(actual.y(),      expected.y())      &&
        nearly_equal(actual.width(),  expected.width())  &&
        nearly_equal(actual.height(), expected.height());
}

QRectF item_rect(const QQuickItem& item)
{
    return QRectF(item.x(), item.y(), item.width(), item.height());
}

bool color_nearly_equal(
    const QColor& actual,
    const QColor& expected)
{
    constexpr int k_tolerance = 2;
    return
        std::abs(actual.red()   - expected.red())   <= k_tolerance &&
        std::abs(actual.green() - expected.green()) <= k_tolerance &&
        std::abs(actual.blue()  - expected.blue())  <= k_tolerance &&
        std::abs(actual.alpha() - expected.alpha()) <= k_tolerance;
}

void compare_font_contract(const QFont& actual, const QFont& expected)
{
    QCOMPARE(actual.pointSizeF(),        expected.pointSizeF());
    QCOMPARE(actual.pixelSize(),         expected.pixelSize());
    QCOMPARE(actual.resolveMask(),       expected.resolveMask());
    QCOMPARE(actual.family(),            expected.family());
    QCOMPARE(actual.families(),          expected.families());
    QCOMPARE(actual.styleName(),         expected.styleName());
    QCOMPARE(actual.weight(),            expected.weight());
    QCOMPARE(actual.style(),             expected.style());
    QCOMPARE(actual.stretch(),           expected.stretch());
    QCOMPARE(actual.hintingPreference(), expected.hintingPreference());
    QCOMPARE(actual.capitalization(),    expected.capitalization());
    QCOMPARE(actual.kerning(),           expected.kerning());
    QCOMPARE(actual.letterSpacingType(), expected.letterSpacingType());
    QCOMPARE(actual.letterSpacing(),     expected.letterSpacing());
    QCOMPARE(actual.wordSpacing(),       expected.wordSpacing());
}

} // namespace

class Vnm_chrome_contract_tests : public QObject
{
    Q_OBJECT

private slots:
    void geometry_helpers_normalize_invalid_dpr()
    {
        using vnm_qml_chrome::normalized_device_pixel_ratio;

        QCOMPARE(normalized_device_pixel_ratio(1.25), 1.25);
        QCOMPARE(normalized_device_pixel_ratio(0.0),  1.0);
        QCOMPARE(normalized_device_pixel_ratio(-2.0), 1.0);
        QCOMPARE(
            normalized_device_pixel_ratio(std::numeric_limits<qreal>::infinity()),
            1.0);
        QCOMPARE(
            normalized_device_pixel_ratio(std::numeric_limits<qreal>::quiet_NaN()),
            1.0);
    }

    void geometry_helpers_snap_custom_frame_rect_to_physical_pixels()
    {
        using vnm_qml_chrome::rect_has_snapped_physical_edges;
        using vnm_qml_chrome::snapped_logical_edge;
        using vnm_qml_chrome::snapped_logical_rect;

        constexpr qreal dpr                  = 1.25;
        constexpr qreal resize_border_width  = 6.0;
        constexpr qreal titlebar_height      = 32.0;
        constexpr qreal content_border_width = 1.0 / dpr;

        const QRectF unsnapped_terminal_rect(
            resize_border_width + content_border_width,
            titlebar_height     + content_border_width,
            1894.4,
            1040.4);

        QVERIFY(!rect_has_snapped_physical_edges(unsnapped_terminal_rect, dpr));
        QVERIFY(nearly_equal(snapped_logical_edge(6.8, dpr), 7.2));

        const QRectF snapped_terminal_rect = snapped_logical_rect(
            unsnapped_terminal_rect,
            dpr);
        const QRectF expected_terminal_rect(7.2, 32.8, 1894.4, 1040.8);

        QVERIFY(rect_has_snapped_physical_edges(snapped_terminal_rect, dpr));
        QVERIFY2(
            rect_nearly_equal(snapped_terminal_rect, expected_terminal_rect),
            "Custom-frame terminal content must snap off the half physical pixel.");
    }

    void geometry_helpers_reject_a_physical_extent_from_another_ratio()
    {
        using vnm_qml_chrome::physical_extent_matches_logical;
        using vnm_qml_chrome::physical_far_edge;
        using vnm_qml_chrome::snapped_logical_edge;

        constexpr qreal dpr = 1.25;

        // A native size is the logical size rounded to whole device pixels, so
        // a matching pair may disagree by up to half a device pixel.
        QVERIFY(physical_extent_matches_logical(755.0, 944.0, dpr));
        QVERIFY(physical_extent_matches_logical(450.0, 562.0, dpr));
        QVERIFY(physical_extent_matches_logical(390.0, 488.0, dpr));

        // A physical extent measured at ratio 1.0 against a held ratio of 1.25,
        // which is what a Windows scale change leaves behind.
        QVERIFY(!physical_extent_matches_logical(755.0, 755.0, dpr));
        QVERIFY(!physical_extent_matches_logical(390.0, 390.0, dpr));

        // The reverse transition, where the held ratio is the lower one.
        QVERIFY(!physical_extent_matches_logical(755.0, 944.0, 1.0));

        QVERIFY(!physical_extent_matches_logical(755.0,   0.0, dpr));
        QVERIFY(!physical_extent_matches_logical(755.0,  -1.0, dpr));
        QVERIFY(!physical_extent_matches_logical(
            755.0,
            std::numeric_limits<qreal>::quiet_NaN(),
            dpr));

        // A rejected extent leaves the edge on the logical extent rather than
        // rescaling it: 755 / 1.25 would be 604.
        QVERIFY(nearly_equal(
            physical_far_edge(755.0, 755.0, dpr),
            snapped_logical_edge(755.0, dpr)));
        QVERIFY(nearly_equal(physical_far_edge(755.0, 944.0, dpr), 755.2));
    }

    void geometry_helpers_distribute_resize_target_around_frame()
    {
        using vnm_qml_chrome::resize_inward_extent;
        using vnm_qml_chrome::resize_outward_extent;

        struct Resize_case {
            qreal frame_extent;
            qreal inward_extent;
            qreal outward_extent;
        };
        const Resize_case cases[] = {
            { 0.0,  3.0,         8.0        },
            { 5.0, 18.0 / 11.0, 48.0 / 11.0 },
            {10.0,  3.0 / 11.0,  8.0 / 11.0 },
            {12.0,  0.0,         0.0        },
        };

        for (const Resize_case& c : cases) {
            QVERIFY(nearly_equal(resize_inward_extent(c.frame_extent), c.inward_extent));
            QVERIFY(nearly_equal(resize_outward_extent(c.frame_extent), c.outward_extent));
        }
    }

    void geometry_singleton_exposes_snapping_contract()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    function snapped_edge() {
        return VNM_chrome_geometry.snapped_logical_edge(6.8, 1.25)
    }

    function snapped_rect() {
        return VNM_chrome_geometry.snapped_logical_rect(
            Qt.rect(6.8, 32.8, 1894.4, 1040.4),
            1.25)
    }

    function invalid_dpr() {
        return VNM_chrome_geometry.normalized_device_pixel_ratio(0)
    }

    function rect_snapped() {
        return VNM_chrome_geometry.rect_has_snapped_physical_edges(snapped_rect(), 1.25)
    }

    function resize_inward_extent() {
        return VNM_chrome_geometry.resize_inward_extent(5, 11)
    }

    function resize_outward_extent() {
        return VNM_chrome_geometry.resize_outward_extent(5, 11)
    }

    function default_resize_target_extent() {
        return VNM_chrome_geometry.default_resize_target_extent
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/geometry_singleton_contract.qml");
        QVERIFY(root != nullptr);

        QVariant snapped_edge;
        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "snapped_edge",
            Q_RETURN_ARG(QVariant, snapped_edge)));
        QVERIFY(nearly_equal(snapped_edge.toReal(), 7.2));

        QVariant snapped_rect;
        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "snapped_rect",
            Q_RETURN_ARG(QVariant, snapped_rect)));
        QVERIFY(rect_nearly_equal(
            snapped_rect.toRectF(),
            QRectF(7.2, 32.8, 1894.4, 1040.8)));

        QVariant invalid_dpr;
        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "invalid_dpr",
            Q_RETURN_ARG(QVariant, invalid_dpr)));
        QCOMPARE(invalid_dpr.toReal(), 1.0);

        QVariant rect_snapped;
        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "rect_snapped",
            Q_RETURN_ARG(QVariant, rect_snapped)));
        QVERIFY(rect_snapped.toBool());

        QVariant default_resize_target;
        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "default_resize_target_extent",
            Q_RETURN_ARG(QVariant, default_resize_target)));
        QCOMPARE(default_resize_target.toReal(), 11.0);

        QVariant resize_inward;
        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "resize_inward_extent",
            Q_RETURN_ARG(QVariant, resize_inward)));
        QVERIFY(nearly_equal(resize_inward.toReal(), 18.0 / 11.0));

        QVariant resize_outward;
        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "resize_outward_extent",
            Q_RETURN_ARG(QVariant, resize_outward)));
        QVERIFY(nearly_equal(resize_outward.toReal(), 48.0 / 11.0));
    }

    void system_window_singleton_exposes_qwindow_wrapper_contract()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import QtQuick.Window
import VNM_Chrome

Window {
    function null_move_returns_false() {
        return VNM_system_window.start_system_move(null)
    }

    function null_resize_returns_false() {
        return VNM_system_window.start_system_resize(null, Qt.LeftEdge)
    }

    function null_stays_on_top_returns_false() {
        return VNM_system_window.set_window_stays_on_top(null, true)
    }

    function invalid_resize_edges_return_false() {
        return !VNM_system_window.start_system_resize(this, 0)
            && !VNM_system_window.start_system_resize(
                this,
                Qt.LeftEdge | Qt.RightEdge)
            && !VNM_system_window.start_system_resize(
                this,
                Qt.TopEdge | Qt.BottomEdge)
            && !VNM_system_window.start_system_resize(
                this,
                Qt.LeftEdge | Qt.RightEdge | Qt.TopEdge | Qt.BottomEdge)
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/system_window_singleton_contract.qml");
        QVERIFY(root != nullptr);

        QVariant move_result;
        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "null_move_returns_false",
            Q_RETURN_ARG(QVariant, move_result)));
        QCOMPARE(move_result.toBool(), false);

        QVariant resize_result;
        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "null_resize_returns_false",
            Q_RETURN_ARG(QVariant, resize_result)));
        QCOMPARE(resize_result.toBool(), false);

        QVariant topmost_result;
        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "null_stays_on_top_returns_false",
            Q_RETURN_ARG(QVariant, topmost_result)));
        QCOMPARE(topmost_result.toBool(), false);

        QVariant invalid_resize_result;
        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "invalid_resize_edges_return_false",
            Q_RETURN_ARG(QVariant, invalid_resize_result)));
        QVERIFY(invalid_resize_result.toBool());
    }

    void system_window_toggles_topmost_hint_without_dropping_other_flags()
    {
        vnm_qml_chrome::System_window system_window;
        QQuickWindow window;
        window.setFlags(
            Qt::Window
            | Qt::FramelessWindowHint
            | Qt::WindowStaysOnBottomHint);

        QVERIFY(system_window.set_window_stays_on_top(&window, true));
        QVERIFY(window.flags().testFlag(Qt::WindowStaysOnTopHint));
        QVERIFY(!window.flags().testFlag(Qt::WindowStaysOnBottomHint));
        QVERIFY(window.flags().testFlag(Qt::FramelessWindowHint));

        QVERIFY(system_window.set_window_stays_on_top(&window, false));
        QVERIFY(!window.flags().testFlag(Qt::WindowStaysOnTopHint));
        QVERIFY(window.flags().testFlag(Qt::FramelessWindowHint));
        QVERIFY(!system_window.set_window_stays_on_top(nullptr, true));
    }

#ifdef Q_OS_WIN
    void system_window_direct_and_owner_associations_have_independent_lifetimes()
    {
        vnm_qml_chrome::System_window system_window;
        auto window_a = std::make_unique<QQuickWindow>();
        auto window_b = std::make_unique<QQuickWindow>();
        auto window_c = std::make_unique<QQuickWindow>();

        window_a->setFlags(Qt::Window | Qt::FramelessWindowHint);
        window_b->setFlags(Qt::Window | Qt::FramelessWindowHint);
        window_c->setFlags(Qt::Window | Qt::FramelessWindowHint);

        // A first owns itself, then becomes a direct target. Replacing A -> A
        // with A -> B must retain A's direct reference; destroying A must remove
        // that reference and B's owner reference without either destruction
        // callback refreshing through the partially destroyed A.
        system_window.track_window_stays_on_top(window_a.get(), window_a.get());
        QVERIFY(system_window.set_window_stays_on_top(window_a.get(), true));
        system_window.track_window_stays_on_top(window_a.get(), window_b.get());
        window_a.reset();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        // A replaceable owner association may overlap direct registrations for
        // both targets. Destroying the owner must not discard either direct
        // membership.
        auto owner = std::make_unique<QObject>();
        QVERIFY(system_window.set_window_stays_on_top(window_b.get(), true));
        {
            vnm_qml_chrome::System_window temporary_facade;
            QVERIFY(temporary_facade.set_window_stays_on_top(window_c.get(), true));
        }
        system_window.track_window_stays_on_top(owner.get(), window_b.get());
        system_window.track_window_stays_on_top(owner.get(), window_c.get());
        owner.reset();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        window_b->show();
        window_c->show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        window_b->setWindowState(Qt::WindowMinimized);
        window_b->setFlag(Qt::WindowStaysOnTopHint, false);
        window_b->hide();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY(window_c->flags().testFlag(Qt::WindowStaysOnTopHint));
        QVERIFY(window_c->isVisible());

        window_b.reset();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        window_c->setWindowState(Qt::WindowMinimized);
        window_c->setWindowState(Qt::WindowNoState);
        window_c->hide();
        window_c->show();
        window_c->setFlag(Qt::WindowStaysOnTopHint, false);
        window_c->setFlag(Qt::WindowStaysOnTopHint, true);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY(window_c->flags().testFlag(Qt::WindowStaysOnTopHint));
    }
#endif

    void foreground_lock_service_source_preserves_lifecycle_and_ownership_invariants()
    {
        const QFileInfo test_source(QString::fromUtf8(__FILE__));
        QFile source_file(test_source.dir().absoluteFilePath(
            QStringLiteral("../src/vnm_system_window.cpp")));
        QVERIFY2(source_file.open(QIODevice::ReadOnly),
            qPrintable(source_file.errorString()));
        const QByteArray source = source_file.readAll();
        const QByteArray normalized_source = source.simplified();

        QVERIFY(source.contains("class Foreground_lock_service : public QObject"));
        QVERIFY(source.contains("QObject(application)"));
        QVERIFY(source.contains("new Foreground_lock_service(application)"));
        QVERIFY(source.contains("associate_direct_target(window)"));
        QVERIFY(source.contains("associate_owner(owner, window)"));
        QVERIFY(source.contains("QHash<QObject*, Owner_association>"));
        QVERIFY(source.contains("QSet<QWindow*>"));

        const qsizetype owner_function = source.indexOf(
            "void associate_owner(QObject* owner, QWindow* window)");
        const qsizetype owner_function_end = source.indexOf(
            "protected:", owner_function);
        QVERIFY(owner_function >= 0);
        QVERIFY(owner_function_end > owner_function);
        const QByteArray owner_source = source.mid(
            owner_function, owner_function_end - owner_function);
        const qsizetype replace_target = owner_source.indexOf(
            "owner_it->window = window;");
        const qsizetype add_target = owner_source.indexOf(
            "add_target_reference(window);", replace_target);
        const qsizetype release_previous = owner_source.indexOf(
            "release_target_reference(previous_window);", add_target);
        QVERIFY(replace_target >= 0);
        QVERIFY(add_target > replace_target);
        QVERIFY(release_previous > add_target);

        for (const QByteArray& connection : {
                 QByteArray("flags_changed"),
                 QByteArray("visibility_changed"),
                 QByteArray("window_state_changed"),
                 QByteArray("active_changed"),
                 QByteArray("destroyed")})
        {
            QVERIFY(normalized_source.contains(
                "QMetaObject::Connection " + connection));
            QVERIFY(normalized_source.contains(
                "QObject::disconnect(association." + connection + ")"));
        }
        QVERIFY(normalized_source.contains(
            "QMetaObject::Connection owner_destroyed"));
        QVERIFY(normalized_source.contains(
            "QObject::disconnect(association.owner_destroyed)"));

        QVERIFY(source.contains("&QWindow::flagsChanged"));
        QVERIFY(source.contains("&QWindow::visibilityChanged"));
        QVERIFY(source.contains("&QWindow::windowStateChanged"));
        QVERIFY(source.contains("&QWindow::activeChanged"));
        QVERIFY(source.contains("&QGuiApplication::applicationStateChanged"));
        QVERIFY(source.contains("&QGuiApplication::focusWindowChanged"));
        QVERIFY(source.contains("m_direct_targets.remove(window)"));
        QVERIFY(source.contains("m_owner_associations.erase(owner_it)"));

        const qsizetype remove_owner = source.indexOf(
            "void remove_owner(QObject* owner)");
        const qsizetype remove_target = source.indexOf(
            "void remove_target(QObject* object)", remove_owner);
        QVERIFY(remove_owner >= 0);
        QVERIFY(remove_target > remove_owner);
        const QByteArray remove_owner_source = source.mid(
            remove_owner, remove_target - remove_owner);
        QVERIFY(remove_owner_source.contains("release_target_reference"));
        QVERIFY(remove_owner_source.contains("schedule_native_lock_refresh()"));
        QVERIFY(!remove_owner_source.contains("refresh_native_lock();"));

        const qsizetype eligibility = source.indexOf(
            "bool has_eligible_target() const");
        QVERIFY(eligibility >= 0);
        const QByteArray eligibility_source = source.mid(eligibility);
        QVERIFY(eligibility_source.contains("m_targets.cbegin()"));
        QVERIFY(eligibility_source.contains("Qt::WindowStaysOnTopHint"));
        QVERIFY(eligibility_source.contains("window->isVisible()"));
        QVERIFY(eligibility_source.contains("QWindow::Minimized"));
        QVERIFY(eligibility_source.contains("Qt::WindowMinimized"));

        const qsizetype refresh = source.indexOf("void refresh_native_lock()");
        const qsizetype lock_call = source.indexOf(
            "LockSetForegroundWindow(LSFW_LOCK) != FALSE", refresh);
        const qsizetype record_lock = source.indexOf(
            "m_native_lock_owned = true", lock_call);
        const qsizetype unlock_guard = source.indexOf(
            "if (!m_native_lock_owned) {", record_lock);
        const qsizetype unlock_call = source.indexOf(
            "LockSetForegroundWindow(LSFW_UNLOCK) != FALSE", unlock_guard);
        QVERIFY(refresh >= 0);
        QVERIFY(lock_call > refresh);
        QVERIFY(record_lock > lock_call);
        QVERIFY(unlock_guard > record_lock);
        QVERIFY(unlock_call > unlock_guard);
        QCOMPARE(
            source.count("LockSetForegroundWindow(LSFW_UNLOCK)"),
            qsizetype(1));
        QVERIFY(source.contains("handle_automatic_native_unlock()"));
        QVERIFY(source.contains("m_native_lock_owned = false"));
        QVERIFY(source.contains("schedule_native_lock_refresh()"));
        QVERIFY(source.contains("m_alt_pressed"));
    }

    void native_frame_normalizes_invalid_frame_width()
    {
        VNM_NativeWindowFrame frame;

        frame.set_frame_width(2.5);
        QCOMPARE(frame.frame_width(), 2.5);

        frame.set_frame_width(-2.0);
        QCOMPARE(frame.frame_width(), 0.0);

        frame.set_frame_width(std::numeric_limits<qreal>::infinity());
        QCOMPARE(frame.frame_width(), 0.0);

        frame.set_frame_width(std::numeric_limits<qreal>::quiet_NaN());
        QCOMPARE(frame.frame_width(), 0.0);
    }

    void native_frame_invalid_or_transparent_color_stays_inactive()
    {
        VNM_NativeWindowFrame frame;

        frame.set_frame_color(QColor());
        QVERIFY(!frame.frame_color().isValid());
        QCOMPARE(frame.active(), false);

        frame.set_frame_color(QColor(18, 52, 86, 120));
        QCOMPARE(frame.frame_color(), QColor(18, 52, 86, 120));
        QCOMPARE(frame.active(), false);
    }

    void native_frame_normalizes_resize_outward_margins()
    {
        VNM_NativeWindowFrame frame;

        frame.set_resize_outward_margins(QMarginsF(4.0, -2.0, 8.0, 3.0));
        QCOMPARE(frame.resize_outward_margins(), QMarginsF(4.0, 0.0, 8.0, 3.0));

        frame.set_resize_outward_margins(QMarginsF(
            std::numeric_limits<qreal>::infinity(),
            std::numeric_limits<qreal>::quiet_NaN(),
            1.0,
            2.0));
        QCOMPARE(frame.resize_outward_margins(), QMarginsF(0.0, 0.0, 1.0, 2.0));
    }

#ifdef Q_OS_WIN
    void native_resize_border_follows_window_geometry()
    {
        QQuickWindow owner;
        owner.setFlags(Qt::Window | Qt::FramelessWindowHint);
        owner.setGeometry(80, 60, 320, 180);
        owner.show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        VNM_NativeWindowFrame frame;
        frame.set_frame_visible(false);
        frame.set_window(&owner);
        frame.set_resize_outward_margins(QMarginsF(4.0, 5.0, 6.0, 7.0));
        frame.set_resize_enabled(true);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        QWindow* resize_border = nullptr;
        for (QWindow* window : QGuiApplication::topLevelWindows()) {
            if (window->objectName() == QStringLiteral("vnm_native_resize_border") &&
                window->transientParent() == &owner)
            {
                resize_border = window;
                break;
            }
        }

        QVERIFY(resize_border != nullptr);
        QVERIFY(resize_border->isVisible());
        QCOMPARE(resize_border->geometry(), owner.geometry().adjusted(-4, -5, 6, 7));
        QVERIFY(!resize_border->mask().isEmpty());

        frame.set_resize_enabled(false);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY(!resize_border->isVisible());
    }
#endif

    void solid_window_example_loads_with_registered_runtime()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        QQmlComponent component(
            &engine,
            QUrl(QStringLiteral("qrc:/examples/solid_window/main.qml")));
        if (!component.isReady()) {
            qWarning().noquote() << component_error_string(component);
        }
        QVERIFY(component.isReady());

        std::unique_ptr<QObject> root(component.create());
        QVERIFY(root != nullptr);

        auto* shell = find_descendant(root.get(), QStringLiteral("chrome_frame_shell"));
        auto* native_frame = root->findChild<VNM_NativeWindowFrame*>();
        QVERIFY(shell        != nullptr);
        QVERIFY(native_frame != nullptr);

        const QMarginsF expected_margins(
            shell->property("left_resize_outward_extent").toReal(),
            shell->property("top_resize_outward_extent").toReal(),
            shell->property("right_resize_outward_extent").toReal(),
            shell->property("bottom_resize_outward_extent").toReal());
        QVERIFY(!expected_margins.isNull());
        QCOMPARE(native_frame->resize_outward_margins(), expected_margins);
    }

    void runtime_bootstrap_registers_manual_qrc_import()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        const QString import_path = QStringLiteral("qrc:/vnm_qml_chrome/qml");
        const QResource qmldir_resource(
            QStringLiteral(":/vnm_qml_chrome/qml/VNM_Chrome/qmldir"));
        QVERIFY(qmldir_resource.isValid());
        QVERIFY(engine.importPathList().contains(import_path));
        QCOMPARE(engine.importPathList().count(import_path), qsizetype{1});
    }

    void exported_types_instantiate_and_expose_contract()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    width: 640
    height: 240

    VNM_ChromeTheme {
        id: chrome_theme
        objectName: "chrome_theme"
    }

    VNM_AnimatedMark {
        objectName: "animated_mark"
        theme: chrome_theme
        x: 0
        y: 0
    }

    VNM_ChromeWindowButton {
        objectName: "window_button"
        theme: chrome_theme
        x: 32
        y: 0
        width: 46
        height: 32
    }

    VNM_TitleBarRoundButton {
        objectName: "round_button"
        theme: chrome_theme
        x: 84
        y: 0
        label: "R"
    }

    VNM_DarkModeTitleButton {
        objectName: "dark_button"
        theme: chrome_theme
        x: 112
        y: 0
    }

    VNM_LanguageTitleButton {
        objectName: "language_button"
        theme: chrome_theme
        x: 140
        y: 0
    }

    VNM_ChromeResizeArea {
        objectName: "resize_area"
        x: 170
        y: 0
        width: 8
        height: 8
        edges: Qt.LeftEdge
    }

    VNM_ChromeSideResizeLayer {
        objectName: "side_layer"
        y: 40
        width: 240
        height: 80
    }

    VNM_ChromeBottomResizeLayer {
        objectName: "bottom_layer"
        y: 130
        width: 240
        height: 8
    }

    VNM_NativeWindowFrame {
        objectName: "native_window_frame"
        frame_visible: true
        frame_width: 2
        frame_color: "#123456"
    }

    VNM_ChromeWindowFrame {
        objectName: "window_frame"
        x: 250
        y: 40
        width: 120
        height: 80
        theme: chrome_theme
        frame_visible: true
        frame_width: 3
        top_edge_visible: false
    }

    VNM_ChromeTitleBar {
        objectName: "chrome_titlebar"
        y: 150
        width: 480
        height: 32
        title: "Contract"
        theme: chrome_theme
        leading_action_component: Component {
            VNM_DarkModeTitleButton {
                theme: chrome_theme
            }
        }
        trailing_action_component: Component {
            VNM_LanguageTitleButton {
                theme: chrome_theme
            }
        }
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/exported_types_contract.qml");
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        QObject* theme = find_descendant(root.get(), QStringLiteral("chrome_theme"));
        QVERIFY(theme != nullptr);
        const char* theme_properties[] = {
            "titlebar",
            "titlebar_text",
            "titlebar_button_icon",
            "titlebar_button_hover",
            "titlebar_button_pressed",
            "titlebar_close_hover",
            "titlebar_close_pressed",
            "titlebar_activity_marker",
            "titlebar_content_border",
            "window_frame_border",
            "round_button_background",
            "round_button_border",
            "round_button_text",
            "mark_grey",
            "mark_orange",
            "mark_underlay",
        };
        for (const char* property_name : theme_properties) {
            QVERIFY2(has_property(theme, property_name), property_name);
        }

        QObject* mark = find_descendant(root.get(), QStringLiteral("animated_mark"));
        QVERIFY(mark != nullptr);
        QVERIFY(has_property(mark, "theme"));
        QVERIFY(has_property(mark, "mark_size"));
        QVERIFY(has_property(mark, "move_enabled"));
        QVERIFY(has_property(mark, "alt_click_enabled"));
        QVERIFY(has_property(mark, "move_drag_threshold"));
        QVERIFY(has_property(mark, "alt_reveal_forced"));
        QVERIFY(has_property(mark, "hover_active"));
        QVERIFY(has_property(mark, "alt_reveal_active"));
        QVERIFY(has_property(mark, "pid_reveal_enabled"));
        QCOMPARE(mark->property("pid_reveal_enabled").toBool(), true);
        QVERIFY(has_property(mark, "stay_on_top_enabled"));
        QCOMPARE(mark->property("stay_on_top_enabled").toBool(), true);
        QVERIFY(has_property(mark, "stay_on_top_active"));
        QCOMPARE(mark->property("stay_on_top_active").toBool(), false);
        QVERIFY(has_property(mark, "pid_phase"));
        QVERIFY(has_property(mark, "pid_layout_width"));
        QVERIFY(has_signal(mark, "move_requested()"));
        QVERIFY(has_signal(mark, "alt_click_requested()"));
        QVERIFY(has_signal(mark, "stay_on_top_change_requested(bool)"));

        QObject* window_button = find_descendant(root.get(), QStringLiteral("window_button"));
        QVERIFY(window_button != nullptr);
        QVERIFY(has_property(window_button, "theme"));
        QVERIFY(has_property(window_button, "background_color"));
        QVERIFY(has_property(window_button, "hover_color"));
        QVERIFY(has_property(window_button, "pressed_color"));
        QVERIFY(has_signal(window_button, "clicked()"));

        QObject* round_button = find_descendant(root.get(), QStringLiteral("round_button"));
        QVERIFY(round_button != nullptr);
        QVERIFY(has_property(round_button, "theme"));
        QVERIFY(has_property(round_button, "label"));
        QVERIFY(has_property(round_button, "tooltip_text"));
        QVERIFY(has_signal(round_button, "clicked()"));

        QObject* dark_button = find_descendant(root.get(), QStringLiteral("dark_button"));
        QVERIFY(dark_button != nullptr);
        QVERIFY(has_property(dark_button, "dark_mode"));
        QVERIFY(has_property(dark_button, "dark_label"));
        QVERIFY(has_property(dark_button, "light_label"));
        QVERIFY(has_signal(dark_button, "toggle_requested()"));

        QObject* language_button = find_descendant(root.get(), QStringLiteral("language_button"));
        QVERIFY(language_button != nullptr);
        QVERIFY(has_property(language_button, "primary_label"));
        QVERIFY(has_property(language_button, "secondary_label"));
        QVERIFY(has_property(language_button, "primary_active"));
        QVERIFY(has_signal(language_button, "toggle_requested()"));

        QObject* resize_area = find_descendant(root.get(), QStringLiteral("resize_area"));
        QVERIFY(resize_area != nullptr);
        QVERIFY(has_property(resize_area, "edges"));
        QVERIFY(has_signal(resize_area, "resize_requested(int)"));

        QObject* side_layer = find_descendant(root.get(), QStringLiteral("side_layer"));
        QVERIFY(side_layer != nullptr);
        QVERIFY(has_property(side_layer, "resize_enabled"));
        QVERIFY(has_property(side_layer, "resize_target_extent"));
        QVERIFY(has_property(side_layer, "left_frame_extent"));
        QVERIFY(has_property(side_layer, "right_frame_extent"));
        QVERIFY(has_signal(side_layer, "resize_requested(int)"));

        QObject* bottom_layer = find_descendant(root.get(), QStringLiteral("bottom_layer"));
        QVERIFY(bottom_layer != nullptr);
        QVERIFY(has_property(bottom_layer, "resize_enabled"));
        QVERIFY(has_property(bottom_layer, "resize_target_extent"));
        QVERIFY(has_property(bottom_layer, "bottom_frame_extent"));
        QVERIFY(has_signal(bottom_layer, "resize_requested(int)"));

        QObject* native_frame = find_descendant(root.get(), QStringLiteral("native_window_frame"));
        QVERIFY(native_frame != nullptr);
        QVERIFY(has_property(native_frame, "window"));
        QVERIFY(has_property(native_frame, "frame_visible"));
        QVERIFY(has_property(native_frame, "frame_width"));
        QVERIFY(has_property(native_frame, "frame_color"));
        QVERIFY(has_property(native_frame, "resize_enabled"));
        QVERIFY(has_property(native_frame, "resize_outward_margins"));
        QVERIFY(has_property(native_frame, "active"));
        QCOMPARE(native_frame->property("frame_visible").toBool(), true);
        QCOMPARE(native_frame->property("frame_width").toReal(), 2.0);
        QCOMPARE(
            object_color(native_frame, "frame_color"),
            QColor(QStringLiteral("#123456")));
        QCOMPARE(native_frame->property("active").toBool(), false);

        QObject* window_frame = find_descendant(root.get(), QStringLiteral("window_frame"));
        QVERIFY(window_frame != nullptr);
        QVERIFY(has_property(window_frame, "theme"));
        QVERIFY(has_property(window_frame, "frame_visible"));
        QVERIFY(has_property(window_frame, "frame_width"));
        QVERIFY(has_property(window_frame, "top_edge_visible"));
        QVERIFY(has_property(window_frame, "bottom_edge_visible"));
        QVERIFY(has_property(window_frame, "left_edge_visible"));
        QVERIFY(has_property(window_frame, "right_edge_visible"));
        QCOMPARE(window_frame->property("frame_visible").toBool(), true);
        QCOMPARE(window_frame->property("frame_width").toReal(), 3.0);
        QCOMPARE(window_frame->property("top_edge_visible").toBool(), false);
        QCOMPARE(window_frame->property("enabled").toBool(), false);
        auto* window_frame_item = qobject_cast<QQuickItem*>(window_frame);
        QVERIFY(window_frame_item != nullptr);
        QCOMPARE(window_frame_item->z(), 1000.0);

        QObject* titlebar = find_descendant(root.get(), QStringLiteral("chrome_titlebar"));
        QVERIFY(titlebar != nullptr);
        const char* titlebar_properties[] = {
            "theme",
            "title",
            "title_font_family",
            "title_editing_enabled",
            "active",
            "maximized",
            "resize_enabled",
            "resize_target_extent",
            "top_frame_extent",
            "left_frame_extent",
            "right_frame_extent",
            "device_pixel_ratio",
            "animated_mark_visible",
            "mark_pid_reveal_enabled",
            "mark_stay_on_top_enabled",
            "window_stays_on_top",
            "activity_marker_text",
            "window_frame_top_visible",
            "window_frame_width",
            "leading_action_component",
            "trailing_action_component",
        };
        for (const char* property_name : titlebar_properties) {
            QVERIFY2(has_property(titlebar, property_name), property_name);
        }
        QCOMPARE(titlebar->property("mark_pid_reveal_enabled").toBool(), true);
        QCOMPARE(titlebar->property("mark_stay_on_top_enabled").toBool(), true);
        QCOMPARE(titlebar->property("window_stays_on_top").toBool(), false);
        QVERIFY(has_signal(titlebar, "move_requested()"));
        QVERIFY(has_signal(titlebar, "resize_requested(int)"));
        QVERIFY(has_signal(titlebar, "minimize_requested()"));
        QVERIFY(has_signal(titlebar, "maximize_toggle_requested()"));
        QVERIFY(has_signal(titlebar, "close_requested()"));
        QVERIFY(has_signal(titlebar, "title_edit_accepted(QString)"));
    }

    void frame_shell_is_importable_from_qrc_and_exposes_contract()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        const QResource shell_resource(
            QStringLiteral(":/vnm_qml_chrome/qml/VNM_Chrome/VNM_ChromeFrameShell.qml"));
        QVERIFY(shell_resource.isValid());

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    width: 420
    height: 160

    VNM_ChromeFrameShell {
        objectName: "frame_shell"
        anchors.fill: parent
        title: "Shell"
        title_font_family: "Caller Supplied Chrome Font"
        frame_color: "#102030"
        frame_outer_edge: 2
        frame_outer_edge_color: "#203040"
        frame_gap: 5
        frame_inner_edge: 3
        frame_inner_edge_color: "#405060"
        resize_target_extent: 11
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/frame_shell_import_contract.qml");
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        QObject* shell = find_descendant(root.get(), QStringLiteral("frame_shell"));
        QVERIFY(shell != nullptr);

        const char* shell_properties[] = {
            "theme",
            "frame_color",
            "frame_outer_edge",
            "frame_outer_edge_color",
            "frame_gap",
            "frame_inner_edge",
            "frame_inner_edge_color",
            "resize_target_extent",
            "device_pixel_ratio",
            "render_target_physical_width",
            "render_target_physical_height",
            "titlebar_height",
            "resize_enabled",
            "title",
            "title_font_family",
            "title_editing_enabled",
            "active",
            "maximized",
            "activity_marker_text",
            "mark_pid_reveal_enabled",
            "mark_stay_on_top_enabled",
            "window_stays_on_top",
            "titlebar_content_left_inset",
            "leading_action_component",
            "trailing_action_component",
            "custom_buttons",
            "minimize_button_visible",
            "maximize_button_visible",
            "close_button_visible",
            "titlebar_item",
            "content_interior_rect",
            "content_interior_x",
            "content_interior_y",
            "content_interior_width",
            "content_interior_height",
        };
        for (const char* property_name : shell_properties) {
            QVERIFY2(has_property(shell, property_name), property_name);
        }

        QVERIFY(has_signal(shell, "move_requested()"));
        QVERIFY(has_signal(shell, "resize_requested(int)"));
        QVERIFY(has_signal(shell, "minimize_requested()"));
        QVERIFY(has_signal(shell, "maximize_toggle_requested()"));
        QVERIFY(has_signal(shell, "close_requested()"));
        QVERIFY(has_signal(shell, "title_edit_accepted(QString)"));

        QObject* shell_titlebar = find_descendant(
            root.get(),
            QStringLiteral("chrome_frame_shell_titlebar"));
        QVERIFY(shell_titlebar != nullptr);
        QCOMPARE(
            shell_titlebar->property("title_font_family").toString(),
            QStringLiteral("Caller Supplied Chrome Font"));
        QVERIFY(find_descendant(
            root.get(),
            QStringLiteral("chrome_frame_shell_content")) != nullptr);
    }

    void frame_shell_draws_outer_gap_inner_edges_with_independent_colors()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    width: 200
    height: 120

    VNM_ChromeTheme {
        id: custom_theme
        titlebar: "#010203"
        window_frame_border: "#111111"
    }

    VNM_ChromeFrameShell {
        objectName: "frame_shell"
        anchors.fill: parent
        theme: custom_theme
        titlebar_height: 32
        device_pixel_ratio: 1
        frame_color: "#102030"
        frame_outer_edge: 2
        frame_outer_edge_color: "#203040"
        frame_gap: 5
        frame_inner_edge: 3
        frame_inner_edge_color: "#405060"
        resize_target_extent: 11
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/frame_shell_edges_contract.qml");
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        auto* shell = find_item(root.get(), QStringLiteral("frame_shell"));
        QVERIFY(shell != nullptr);

        auto* fill = find_item(root.get(), QStringLiteral("chrome_frame_shell_frame_fill"));
        QVERIFY(fill != nullptr);
        QCOMPARE(object_color(fill, "color"), QColor(QStringLiteral("#102030")));
        auto* fill_top = find_item(root.get(), QStringLiteral("chrome_frame_shell_frame_fill_top"));
        auto* fill_bottom = find_item(root.get(), QStringLiteral("chrome_frame_shell_frame_fill_bottom"));
        auto* fill_left = find_item(root.get(), QStringLiteral("chrome_frame_shell_frame_fill_left"));
        auto* fill_right = find_item(root.get(), QStringLiteral("chrome_frame_shell_frame_fill_right"));
        QVERIFY(fill_top    != nullptr);
        QVERIFY(fill_bottom != nullptr);
        QVERIFY(fill_left   != nullptr);
        QVERIFY(fill_right  != nullptr);
        QVERIFY(rect_nearly_equal(item_rect(*fill_top),    QRectF(0.0,   0.0, 200.0, 35.0)));
        QVERIFY(rect_nearly_equal(item_rect(*fill_bottom), QRectF(0.0, 110.0, 200.0, 10.0)));
        QVERIFY(rect_nearly_equal(item_rect(*fill_left),   QRectF(0.0,  35.0,  10.0, 75.0)));
        QVERIFY(rect_nearly_equal(item_rect(*fill_right),  QRectF(190.0, 35.0,  10.0, 75.0)));
        QCOMPARE(object_color(fill_left, "color"), QColor(QStringLiteral("#102030")));

        auto* outer_top = find_item(root.get(), QStringLiteral("chrome_window_frame_top"));
        auto* outer_bottom = find_item(root.get(), QStringLiteral("chrome_window_frame_bottom"));
        auto* outer_left = find_item(root.get(), QStringLiteral("chrome_window_frame_left"));
        auto* outer_right = find_item(root.get(), QStringLiteral("chrome_window_frame_right"));
        QVERIFY(outer_top    != nullptr);
        QVERIFY(outer_bottom != nullptr);
        QVERIFY(outer_left   != nullptr);
        QVERIFY(outer_right  != nullptr);

        QVERIFY(!outer_top->isVisible());
        QVERIFY(outer_bottom->isVisible());
        QVERIFY(outer_left->isVisible());
        QVERIFY(outer_right->isVisible());
        QVERIFY(rect_nearly_equal(item_rect(*outer_bottom), QRectF(0.0, 118.0, 200.0, 2.0)));
        QVERIFY(rect_nearly_equal(item_rect(*outer_left),   QRectF(0.0,   0.0,   2.0, 120.0)));
        QVERIFY(rect_nearly_equal(item_rect(*outer_right),  QRectF(198.0, 0.0,   2.0, 120.0)));
        QCOMPARE(object_color(outer_left, "color"), QColor(QStringLiteral("#203040")));

        auto* titlebar_top = find_item(root.get(), QStringLiteral("titlebar_window_frame_top"));
        QVERIFY(titlebar_top != nullptr);
        QVERIFY(titlebar_top->isVisible());
        QCOMPARE(titlebar_top->z(), 3.0);
        QVERIFY(rect_nearly_equal(item_rect(*titlebar_top), QRectF(0.0, 0.0, 200.0, 2.0)));
        QCOMPARE(object_color(titlebar_top, "color"), QColor(QStringLiteral("#203040")));

        auto* mark = find_item(root.get(), QStringLiteral("vnm_animated_mark"));
        QVERIFY(mark != nullptr);
        QVERIFY(mark->parentItem() != nullptr);
        QVERIFY(titlebar_top->z() > mark->parentItem()->z());

        auto* inner_top = find_item(root.get(), QStringLiteral("chrome_frame_shell_inner_edge_top"));
        auto* inner_bottom = find_item(root.get(), QStringLiteral("chrome_frame_shell_inner_edge_bottom"));
        auto* inner_left = find_item(root.get(), QStringLiteral("chrome_frame_shell_inner_edge_left"));
        auto* inner_right = find_item(root.get(), QStringLiteral("chrome_frame_shell_inner_edge_right"));
        QVERIFY(inner_top    != nullptr);
        QVERIFY(inner_bottom != nullptr);
        QVERIFY(inner_left   != nullptr);
        QVERIFY(inner_right  != nullptr);

        QVERIFY(rect_nearly_equal(item_rect(*inner_top),    QRectF(7.0,   32.0, 186.0, 3.0)));
        QVERIFY(rect_nearly_equal(item_rect(*inner_bottom), QRectF(7.0,  110.0, 186.0, 3.0)));
        QVERIFY(rect_nearly_equal(item_rect(*inner_left),   QRectF(7.0,   32.0,   3.0, 81.0)));
        QVERIFY(rect_nearly_equal(item_rect(*inner_right),  QRectF(190.0, 32.0,   3.0, 81.0)));
        QCOMPARE(object_color(inner_left, "color"), QColor(QStringLiteral("#405060")));

        QVERIFY(nearly_equal(inner_left->x() - outer_left->width(), 5.0));
        QVERIFY(nearly_equal(outer_right->x() - (inner_right->x() + inner_right->width()), 5.0));
        QVERIFY(nearly_equal(outer_bottom->y() - (inner_bottom->y() + inner_bottom->height()), 5.0));
        QVERIFY(rect_nearly_equal(
            shell->property("content_interior_rect").toRectF(),
            QRectF(10.0, 35.0, 180.0, 75.0)));

        auto* left_resize_area = find_item(root.get(), QStringLiteral("left_resize_area"));
        auto* right_resize_area = find_item(root.get(), QStringLiteral("right_resize_area"));
        auto* bottom_resize_area = find_item(root.get(), QStringLiteral("bottom_resize_area"));
        auto* top_left_resize_area = find_item(root.get(), QStringLiteral("top_left_resize_area"));
        QVERIFY(left_resize_area     != nullptr);
        QVERIFY(right_resize_area    != nullptr);
        QVERIFY(bottom_resize_area   != nullptr);
        QVERIFY(top_left_resize_area != nullptr);
        QVERIFY(nearly_equal(left_resize_area->x(), -8.0 / 11.0));
        QVERIFY(nearly_equal(left_resize_area->width(), 11.0));
        QVERIFY(nearly_equal(right_resize_area->x(), 200.0 - 10.0 - 3.0 / 11.0));
        QVERIFY(nearly_equal(right_resize_area->width(), 11.0));
        QVERIFY(nearly_equal(bottom_resize_area->y(), 8.0 / 11.0));
        QVERIFY(nearly_equal(bottom_resize_area->height(), 11.0));
        QVERIFY(nearly_equal(top_left_resize_area->width(), 11.0));
        QVERIFY(nearly_equal(
            shell->property("left_resize_outward_extent").toReal(),
            8.0 / 11.0));
    }

    void frame_shell_preserves_dpr_125_scalar_geometry_examples()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    width: 200
    height: 120

    VNM_ChromeFrameShell {
        objectName: "frame_shell"
        anchors.fill: parent
        titlebar_height: 32
        device_pixel_ratio: 1.25
        frame_gap: 4
        frame_outer_edge: 0.8
        frame_inner_edge: 0.8
        resize_target_extent: 4.8
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/frame_shell_dpr_125_contract.qml");
        QVERIFY(root != nullptr);

        auto* shell = find_item(root.get(), QStringLiteral("frame_shell"));
        auto* inner_left = find_item(root.get(), QStringLiteral("chrome_frame_shell_inner_edge_left"));
        auto* titlebar_top = find_item(root.get(), QStringLiteral("titlebar_window_frame_top"));
        auto* outer_left = find_item(root.get(), QStringLiteral("chrome_window_frame_left"));
        QVERIFY(shell        != nullptr);
        QVERIFY(inner_left   != nullptr);
        QVERIFY(titlebar_top != nullptr);
        QVERIFY(outer_left   != nullptr);

        struct shell_scalar_case_t {
            qreal outer_edge;
            qreal inner_edge;
            qreal expected_inner_x;
            qreal expected_content_x;
            bool  outer_visible;
            bool  inner_visible;
        };

        const shell_scalar_case_t cases[] = {
            {0.8, 0.8, 4.8, 5.6, true,  true },
            {0.0, 0.8, 4.0, 4.8, false, true },
            {0.8, 0.0, 4.8, 4.8, true,  false},
            {0.0, 0.0, 4.0, 4.0, false, false},
        };

        for (const shell_scalar_case_t& c : cases) {
            QVERIFY(shell->setProperty("frame_outer_edge", c.outer_edge));
            QVERIFY(shell->setProperty("frame_inner_edge", c.inner_edge));
            shell->ensurePolished();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

            QVERIFY(nearly_equal(inner_left->x(), c.expected_inner_x));
            QVERIFY(nearly_equal(
                shell->property("content_interior_x").toReal(),
                c.expected_content_x));
            QCOMPARE(titlebar_top->isVisible(), c.outer_visible);
            QCOMPARE(outer_left->isVisible(), c.outer_visible);
            QCOMPARE(inner_left->isVisible(), c.inner_visible);

            const qreal expected_inner_width = c.inner_visible ? 0.8 : 0.0;
            QVERIFY(nearly_equal(inner_left->width(), expected_inner_width));
            QVERIFY(vnm_qml_chrome::rect_has_snapped_physical_edges(
                shell->property("content_interior_rect").toRectF(),
                1.25));
        }

        QVERIFY(nearly_equal(shell->property("content_interior_y").toReal(), 32.0));
        QVERIFY(shell->setProperty("frame_inner_edge", 0.8));
        shell->ensurePolished();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY(nearly_equal(shell->property("content_interior_y").toReal(), 32.8));
    }

    void frame_shell_snaps_right_and_bottom_inner_edges_from_primitives()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    width: 200
    height: 120

    VNM_ChromeFrameShell {
        objectName: "frame_shell"
        anchors.fill: parent
        titlebar_height: 32
        device_pixel_ratio: 1.25
        frame_outer_edge: 1
        frame_gap: 4
        frame_inner_edge: 1.04
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine,
            qml_source,
            "qrc:/tests/frame_shell_far_edge_snapping_contract.qml");
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        auto* shell = find_item(root.get(), QStringLiteral("frame_shell"));
        auto* outer_left = find_item(
            root.get(),
            QStringLiteral("chrome_window_frame_left"));
        auto* outer_bottom = find_item(
            root.get(),
            QStringLiteral("chrome_window_frame_bottom"));
        auto* titlebar_top = find_item(
            root.get(),
            QStringLiteral("titlebar_window_frame_top"));
        auto* inner_bottom = find_item(
            root.get(),
            QStringLiteral("chrome_frame_shell_inner_edge_bottom"));
        auto* inner_right = find_item(
            root.get(),
            QStringLiteral("chrome_frame_shell_inner_edge_right"));
        QVERIFY(shell         != nullptr);
        QVERIFY(outer_left    != nullptr);
        QVERIFY(outer_bottom  != nullptr);
        QVERIFY(titlebar_top  != nullptr);
        QVERIFY(inner_bottom  != nullptr);
        QVERIFY(inner_right   != nullptr);

        QVERIFY(rect_nearly_equal(
            item_rect(*outer_left),
            QRectF(0.0, 0.0, 0.8, 120.0)));
        QVERIFY(rect_nearly_equal(
            item_rect(*outer_bottom),
            QRectF(0.0, 119.0, 200.0, 1.0)));
        QVERIFY(rect_nearly_equal(
            item_rect(*titlebar_top),
            QRectF(0.0, 0.0, 200.0, 0.8)));
        QVERIFY(rect_nearly_equal(
            item_rect(*inner_bottom),
            QRectF(4.8, 113.6, 190.4, 1.6)));
        QVERIFY(rect_nearly_equal(
            item_rect(*inner_right),
            QRectF(193.6, 32.0, 1.6, 83.2)));
        QVERIFY(nearly_equal(shell->property("content_interior_x").toReal(), 6.4));
        QVERIFY(nearly_equal(shell->property("content_interior_width").toReal(), 187.2));
        QVERIFY(vnm_qml_chrome::rect_has_snapped_physical_edges(
            shell->property("content_interior_rect").toRectF(),
            1.25));
    }

    void frame_shell_forwards_titlebar_properties_and_window_command_signals()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    width: 500
    height: 140

    VNM_ChromeFrameShell {
        objectName: "frame_shell"
        anchors.fill: parent
        title: "Shell Commands"
        title_editing_enabled: true
        active: false
        maximized: true
        activity_marker_text: "!"
        minimize_button_visible: false
        maximize_button_visible: false
        close_button_visible: true
        leading_action_component: Component {
            Item {
                objectName: "shell_leading_action"
                width: 10
                height: 10
            }
        }
        trailing_action_component: Component {
            Item {
                objectName: "shell_trailing_action"
                width: 12
                height: 10
            }
        }
        custom_buttons: [{
            object_name: "shell_custom_button",
            width: 38
        }]
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/frame_shell_titlebar_forwarding_contract.qml");
        QVERIFY(root != nullptr);
        auto* root_item = qobject_cast<QQuickItem*>(root.get());
        QVERIFY(root_item != nullptr);
        root_item->ensurePolished();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        QObject* shell = find_descendant(root.get(), QStringLiteral("frame_shell"));
        QObject* titlebar = find_descendant(
            root.get(),
            QStringLiteral("chrome_frame_shell_titlebar"));
        QObject* animated_mark = find_descendant(
            root.get(),
            QStringLiteral("vnm_animated_mark"));
        QVERIFY(shell    != nullptr);
        QVERIFY(titlebar != nullptr);
        QVERIFY(animated_mark != nullptr);

        QCOMPARE(titlebar->property("title").toString(), QStringLiteral("Shell Commands"));
        QCOMPARE(titlebar->property("title_editing_enabled").toBool(), true);
        QCOMPARE(titlebar->property("active").toBool(), false);
        QCOMPARE(titlebar->property("maximized").toBool(), true);
        QCOMPARE(shell->property("mark_pid_reveal_enabled").toBool(), true);
        QCOMPARE(titlebar->property("mark_pid_reveal_enabled").toBool(), true);
        QCOMPARE(animated_mark->property("pid_reveal_enabled").toBool(), true);
        QCOMPARE(shell->property("mark_stay_on_top_enabled").toBool(), true);
        QCOMPARE(titlebar->property("mark_stay_on_top_enabled").toBool(), true);
        QCOMPARE(animated_mark->property("stay_on_top_enabled").toBool(), true);
        QCOMPARE(shell->property("window_stays_on_top").toBool(), false);
        QCOMPARE(titlebar->property("activity_marker_text").toString(), QStringLiteral("!"));
        QVERIFY(shell->setProperty("title", QStringLiteral("Updated Shell")));
        QCOMPARE(titlebar->property("title").toString(), QStringLiteral("Updated Shell"));
        QVERIFY(shell->setProperty("mark_pid_reveal_enabled", false));
        QCOMPARE(titlebar->property("mark_pid_reveal_enabled").toBool(), false);
        QCOMPARE(animated_mark->property("pid_reveal_enabled").toBool(), false);
        QVERIFY(shell->setProperty("mark_stay_on_top_enabled", false));
        QCOMPARE(titlebar->property("mark_stay_on_top_enabled").toBool(), false);
        QCOMPARE(animated_mark->property("stay_on_top_enabled").toBool(), false);

        QVERIFY(find_item(root.get(), QStringLiteral("shell_leading_action")) != nullptr);
        QVERIFY(find_item(root.get(), QStringLiteral("shell_trailing_action")) != nullptr);
        auto* custom_button = find_item(root.get(), QStringLiteral("shell_custom_button"));
        auto* minimize_button = find_item(root.get(), QStringLiteral("minimize_button"));
        auto* maximize_button = find_item(root.get(), QStringLiteral("maximize_button"));
        auto* close_button = find_item(root.get(), QStringLiteral("close_button"));
        QVERIFY(custom_button   != nullptr);
        QVERIFY(minimize_button != nullptr);
        QVERIFY(maximize_button != nullptr);
        QVERIFY(close_button    != nullptr);
        QVERIFY(custom_button->isVisible());
        QVERIFY(!minimize_button->isVisible());
        QVERIFY(!maximize_button->isVisible());
        QVERIFY(close_button->isVisible());

        QSignalSpy move_spy(shell, SIGNAL(move_requested()));
        QSignalSpy resize_spy(shell, SIGNAL(resize_requested(int)));
        QSignalSpy minimize_spy(shell, SIGNAL(minimize_requested()));
        QSignalSpy maximize_spy(shell, SIGNAL(maximize_toggle_requested()));
        QSignalSpy close_spy(shell, SIGNAL(close_requested()));
        QSignalSpy title_edit_spy(shell, SIGNAL(title_edit_accepted(QString)));
        QVERIFY(move_spy.isValid());
        QVERIFY(resize_spy.isValid());
        QVERIFY(minimize_spy.isValid());
        QVERIFY(maximize_spy.isValid());
        QVERIFY(close_spy.isValid());
        QVERIFY(title_edit_spy.isValid());

        QVERIFY(QMetaObject::invokeMethod(titlebar, "move_requested"));
        QVERIFY(QMetaObject::invokeMethod(
            titlebar,
            "resize_requested",
            Q_ARG(int, int(Qt::RightEdge))));
        QVERIFY(QMetaObject::invokeMethod(titlebar, "minimize_requested"));
        QVERIFY(QMetaObject::invokeMethod(titlebar, "maximize_toggle_requested"));
        QVERIFY(QMetaObject::invokeMethod(titlebar, "close_requested"));
        QVERIFY(QMetaObject::invokeMethod(
            titlebar,
            "title_edit_accepted",
            Q_ARG(QString, QStringLiteral("User title"))));

        QCOMPARE(move_spy.count(), 1);
        QCOMPARE(resize_spy.count(), 1);
        QCOMPARE(resize_spy.takeFirst().at(0).toInt(), int(Qt::RightEdge));
        QCOMPARE(minimize_spy.count(), 1);
        QCOMPARE(maximize_spy.count(), 1);
        QCOMPARE(close_spy.count(), 1);
        QCOMPARE(title_edit_spy.count(), 1);
        QCOMPARE(
            title_edit_spy.takeFirst().at(0).toString(),
            QStringLiteral("User title"));
    }

    void frame_shell_forwards_edge_resize_requests_and_disable_state()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    width: 240
    height: 120

    VNM_ChromeFrameShell {
        objectName: "frame_shell"
        anchors.fill: parent
        titlebar_height: 32
        resize_target_extent: 8
        resize_enabled: true
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine,
            qml_source,
            "qrc:/tests/frame_shell_resize_forwarding_contract.qml");
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        QObject* shell = find_descendant(root.get(), QStringLiteral("frame_shell"));
        QVERIFY(shell != nullptr);
        QSignalSpy resize_spy(shell, SIGNAL(resize_requested(int)));
        QVERIFY(resize_spy.isValid());

        struct resize_area_case_t {
            const char* object_name;
            int         edges;
        };

        const resize_area_case_t cases[] = {
            {"left_resize_area",         int(Qt::LeftEdge) },
            {"right_resize_area",        int(Qt::RightEdge)},
            {"bottom_resize_area",       int(Qt::BottomEdge)},
            {"bottom_left_resize_area",  int(Qt::LeftEdge  | Qt::BottomEdge)},
            {"bottom_right_resize_area", int(Qt::RightEdge | Qt::BottomEdge)},
            {"top_left_resize_area",     int(Qt::LeftEdge  | Qt::TopEdge)},
            {"top_right_resize_area",    int(Qt::RightEdge | Qt::TopEdge)},
        };

        for (const resize_area_case_t& c : cases) {
            QObject* area = find_descendant(root.get(), QString::fromLatin1(c.object_name));
            QVERIFY2(area != nullptr, c.object_name);
            QVERIFY2(area->property("enabled").toBool(), c.object_name);
            QVERIFY(QMetaObject::invokeMethod(
                area,
                "resize_requested",
                Q_ARG(int, c.edges)));
            QCOMPARE(resize_spy.count(), 1);
            QCOMPARE(resize_spy.takeFirst().at(0).toInt(), c.edges);
        }

        QVERIFY(shell->setProperty("resize_enabled", false));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        for (const resize_area_case_t& c : cases) {
            QObject* area = find_descendant(root.get(), QString::fromLatin1(c.object_name));
            QVERIFY2(area != nullptr, c.object_name);
            QVERIFY2(!area->property("enabled").toBool(), c.object_name);
            QVERIFY2(!area->property("visible").toBool(), c.object_name);
        }
    }

    void frame_shell_places_non_terminal_content_inside_content_interior()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    width: 200
    height: 120

    VNM_ChromeFrameShell {
        objectName: "frame_shell"
        anchors.fill: parent
        titlebar_height: 30
        device_pixel_ratio: 1
        frame_outer_edge: 2
        frame_gap: 4
        frame_inner_edge: 1

        Rectangle {
            objectName: "payload"
            anchors.fill: parent
            color: "#506070"
        }
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/frame_shell_content_slot_contract.qml");
        QVERIFY(root != nullptr);
        auto* root_item = qobject_cast<QQuickItem*>(root.get());
        QVERIFY(root_item != nullptr);
        root_item->ensurePolished();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        auto* shell = find_item(root.get(), QStringLiteral("frame_shell"));
        auto* content = find_item(root.get(), QStringLiteral("chrome_frame_shell_content"));
        auto* payload = find_item(root.get(), QStringLiteral("payload"));
        QVERIFY(shell   != nullptr);
        QVERIFY(content != nullptr);
        QVERIFY(payload != nullptr);
        QCOMPARE(payload->parentItem(), content);

        const QRectF expected_content_rect(7.0, 31.0, 186.0, 82.0);
        QVERIFY(rect_nearly_equal(item_rect(*content), expected_content_rect));
        QVERIFY(rect_nearly_equal(
            shell->property("content_interior_rect").toRectF(),
            expected_content_rect));
        QVERIFY(rect_nearly_equal(item_rect(*payload), QRectF(0.0, 0.0, 186.0, 82.0)));

        const QPointF payload_origin = payload->mapToItem(shell, QPointF(0.0, 0.0));
        QVERIFY(nearly_equal(payload_origin.x(), expected_content_rect.x()));
        QVERIFY(nearly_equal(payload_origin.y(), expected_content_rect.y()));
    }

    void window_frame_overlays_edges_without_reserving_space()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    width: 120
    height: 80

    VNM_ChromeTheme {
        id: custom_theme
        window_frame_border: "#445566"
    }

    VNM_ChromeWindowFrame {
        objectName: "window_frame"
        anchors.fill: parent
        theme: custom_theme
        frame_width: 3
        top_edge_visible: false
        right_edge_visible: false
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/window_frame_overlay_contract.qml");
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        auto* frame = qobject_cast<QQuickItem*>(
            find_descendant(root.get(), QStringLiteral("window_frame")));
        QVERIFY(frame != nullptr);
        QCOMPARE(frame->isEnabled(), false);
        QCOMPARE(frame->z(), 1000.0);
        QVERIFY(rect_nearly_equal(item_rect(*frame), QRectF(0.0, 0.0, 120.0, 80.0)));

        auto* top = qobject_cast<QQuickItem*>(
            find_descendant(root.get(), QStringLiteral("chrome_window_frame_top")));
        auto* bottom = qobject_cast<QQuickItem*>(
            find_descendant(root.get(), QStringLiteral("chrome_window_frame_bottom")));
        auto* left = qobject_cast<QQuickItem*>(
            find_descendant(root.get(), QStringLiteral("chrome_window_frame_left")));
        auto* right = qobject_cast<QQuickItem*>(
            find_descendant(root.get(), QStringLiteral("chrome_window_frame_right")));
        QVERIFY(top    != nullptr);
        QVERIFY(bottom != nullptr);
        QVERIFY(left   != nullptr);
        QVERIFY(right  != nullptr);

        QVERIFY(!top->isVisible());
        QVERIFY(bottom->isVisible());
        QVERIFY(left->isVisible());
        QVERIFY(!right->isVisible());
        QVERIFY(rect_nearly_equal(item_rect(*top),    QRectF(0.0,   0.0, 120.0, 3.0)));
        QVERIFY(rect_nearly_equal(item_rect(*bottom), QRectF(0.0,  77.0, 120.0, 3.0)));
        QVERIFY(rect_nearly_equal(item_rect(*left),   QRectF(0.0,   0.0,   3.0, 80.0)));
        QVERIFY(rect_nearly_equal(item_rect(*right),  QRectF(117.0, 0.0,   3.0, 80.0)));
        QCOMPARE(object_color(bottom, "color"), QColor(QStringLiteral("#445566")));
    }

    void frame_shell_snaps_outer_right_and_bottom_edges_from_primitives()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    width: 201
    height: 121

    VNM_ChromeFrameShell {
        objectName: "frame_shell"
        anchors.fill: parent
        titlebar_height: 32
        frame_outer_edge: 1
        frame_gap: 4
        frame_inner_edge: 1
        device_pixel_ratio: 1.25
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine,
            qml_source,
            "qrc:/tests/frame_shell_outer_far_edge_snapping_contract.qml");
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        auto* top = find_item(root.get(), QStringLiteral("chrome_window_frame_top"));
        auto* bottom = find_item(root.get(), QStringLiteral("chrome_window_frame_bottom"));
        auto* left = find_item(root.get(), QStringLiteral("chrome_window_frame_left"));
        auto* right = find_item(root.get(), QStringLiteral("chrome_window_frame_right"));
        QVERIFY(top    != nullptr);
        QVERIFY(bottom != nullptr);
        QVERIFY(left   != nullptr);
        QVERIFY(right  != nullptr);

        QVERIFY(!top->isVisible());
        QVERIFY(rect_nearly_equal(item_rect(*top),    QRectF(0.0,   0.0, 200.8,   0.8)));
        QVERIFY(rect_nearly_equal(item_rect(*bottom), QRectF(0.0, 120.0, 200.8,   0.8)));
        QVERIFY(rect_nearly_equal(item_rect(*left),   QRectF(0.0,   0.0,   0.8, 120.8)));
        QVERIFY(rect_nearly_equal(item_rect(*right),  QRectF(200.0, 0.0,   0.8, 120.8)));
        QVERIFY(vnm_qml_chrome::rect_has_snapped_physical_edges(item_rect(*bottom), 1.25));
        QVERIFY(vnm_qml_chrome::rect_has_snapped_physical_edges(item_rect(*right),  1.25));
    }

    void frame_shell_uses_render_target_physical_extent_for_far_edges()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    id: root

    width: 1
    height: 1

    VNM_ChromeFrameShell {
        objectName: "frame_shell"
        anchors.fill: parent
        titlebar_height: 32
        frame_outer_edge: 1
        frame_gap: 3
        frame_inner_edge: 1
        device_pixel_ratio: 1.25
        render_target_physical_width: root.render_target_physical_width
        render_target_physical_height: root.render_target_physical_height
    }

    property real render_target_physical_width: 0
    property real render_target_physical_height: 0
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine,
            qml_source,
            "qrc:/tests/frame_shell_render_target_extent_contract.qml");
        QVERIFY(root != nullptr);

        auto* outer_bottom = find_item(root.get(), QStringLiteral("chrome_window_frame_bottom"));
        auto* outer_right = find_item(root.get(), QStringLiteral("chrome_window_frame_right"));
        auto* inner_bottom = find_item(root.get(), QStringLiteral("chrome_frame_shell_inner_edge_bottom"));
        auto* inner_right = find_item(root.get(), QStringLiteral("chrome_frame_shell_inner_edge_right"));
        QVERIFY(outer_bottom != nullptr);
        QVERIFY(outer_right  != nullptr);
        QVERIFY(inner_bottom != nullptr);
        QVERIFY(inner_right  != nullptr);

        struct extent_case_t {
            qreal  width;
            qreal  height;
            qreal  physical_width;
            qreal  physical_height;
            QRectF expected_outer_bottom;
            QRectF expected_outer_right;
            QRectF expected_inner_bottom;
            QRectF expected_inner_right;
        };

        const extent_case_t cases[] = {
            {
                450.0,
                282.0,
                562.0,
                352.0,
                QRectF(0.0, 280.8, 449.6, 0.8),
                QRectF(448.8, 0.0, 0.8, 281.6),
                QRectF(4.0, 276.8, 441.6, 0.8),
                QRectF(444.8, 32.0, 0.8, 245.6),
            },
            {
                566.0,
                406.0,
                707.0,
                507.0,
                QRectF(0.0, 404.8, 565.6, 0.8),
                QRectF(564.8, 0.0, 0.8, 405.6),
                QRectF(4.0, 400.8, 557.6, 0.8),
                QRectF(560.8, 32.0, 0.8, 369.6),
            },
            {
                610.0,
                310.0,
                763.0,
                388.0,
                QRectF(0.0, 309.6, 610.4, 0.8),
                QRectF(609.6, 0.0, 0.8, 310.4),
                QRectF(4.0, 305.6, 602.4, 0.8),
                QRectF(605.6, 32.0, 0.8, 274.4),
            },
            {
                618.0,
                346.0,
                772.0,
                432.0,
                QRectF(0.0, 344.8, 617.6, 0.8),
                QRectF(616.8, 0.0, 0.8, 345.6),
                QRectF(4.0, 340.8, 609.6, 0.8),
                QRectF(612.8, 32.0, 0.8, 309.6),
            },
        };

        for (const extent_case_t& c : cases) {
            auto* root_item = qobject_cast<QQuickItem*>(root.get());
            QVERIFY(root_item != nullptr);
            root_item->setSize(QSizeF(c.width, c.height));
            QVERIFY(root->setProperty("render_target_physical_width", c.physical_width));
            QVERIFY(root->setProperty("render_target_physical_height", c.physical_height));
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

            QVERIFY(rect_nearly_equal(item_rect(*outer_bottom), c.expected_outer_bottom));
            QVERIFY(rect_nearly_equal(item_rect(*outer_right),  c.expected_outer_right));
            QVERIFY(rect_nearly_equal(item_rect(*inner_bottom), c.expected_inner_bottom));
            QVERIFY(rect_nearly_equal(item_rect(*inner_right),  c.expected_inner_right));
        }
    }

    void frame_shell_rejects_physical_extent_from_a_stale_device_pixel_ratio()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        // A display scale change moves the device-pixel ratio and the reported
        // physical window size together. Whenever a shell samples the two at
        // different ratios the quotient is meaningless, so the physical extent
        // must be discarded rather than allowed to rescale the interior. Here
        // the physical size follows a ratio of 1.0 while the shell still holds
        // the previous 1.25, which is the shape of a Windows scaling change.
        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    width: 755
    height: 390

    VNM_ChromeFrameShell {
        objectName: "desynced_shell"
        anchors.fill: parent
        titlebar_height: 32
        frame_outer_edge: 1
        frame_gap: 3
        frame_inner_edge: 1
        device_pixel_ratio: 1.25
        render_target_physical_width: 755
        render_target_physical_height: 390
    }

    VNM_ChromeFrameShell {
        objectName: "logical_shell"
        anchors.fill: parent
        titlebar_height: 32
        frame_outer_edge: 1
        frame_gap: 3
        frame_inner_edge: 1
        device_pixel_ratio: 1.25
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine,
            qml_source,
            "qrc:/tests/frame_shell_stale_device_pixel_ratio_contract.qml");
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        auto* desynced_shell = find_item(root.get(), QStringLiteral("desynced_shell"));
        auto* logical_shell  = find_item(root.get(), QStringLiteral("logical_shell"));
        QVERIFY(desynced_shell != nullptr);
        QVERIFY(logical_shell  != nullptr);

        const QRectF desynced_interior =
            desynced_shell->property("content_interior_rect").toRectF();
        const QRectF logical_interior =
            logical_shell->property("content_interior_rect").toRectF();

        // Guard the comparison itself: two shells that both collapsed would
        // otherwise satisfy the equality below.
        QVERIFY(logical_interior.width()  > 700.0);
        QVERIFY(logical_interior.height() > 330.0);

        QVERIFY(rect_nearly_equal(desynced_interior, logical_interior));

        auto* desynced_content = find_item(
            desynced_shell,
            QStringLiteral("chrome_frame_shell_content"));
        QVERIFY(desynced_content != nullptr);
        QVERIFY(rect_nearly_equal(item_rect(*desynced_content), logical_interior));
    }

    void frame_shell_reads_device_pixel_ratio_from_its_window()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        // The window ratio is the only live source: the Screen attached property
        // never re-notifies while a window stays on the same QScreen, so a shell
        // bound to it keeps the ratio of a previous display scale forever.
        static const char qml_source[] = R"(
import QtQuick
import QtQuick.Window
import VNM_Chrome

Window {
    objectName: "host_window"
    width: 480
    height: 320

    VNM_ChromeFrameShell {
        objectName: "frame_shell"
        anchors.fill: parent
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine,
            qml_source,
            "qrc:/tests/frame_shell_window_device_pixel_ratio_contract.qml");
        QVERIFY(root != nullptr);

        auto* window = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(window != nullptr);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        auto* shell = find_item(root.get(), QStringLiteral("frame_shell"));
        QVERIFY(shell != nullptr);

        const qreal shell_ratio = shell->property("device_pixel_ratio").toReal();
        QVERIFY(shell_ratio > 0.0);
        QVERIFY(nearly_equal(shell_ratio, window->effectiveDevicePixelRatio()));
    }

    void frame_shell_terminal_chrome_renders_outer_edge_pixels()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    id: root

    property real test_device_pixel_ratio: 1
    readonly property real frame_edge:
        1 / VNM_chrome_geometry.normalized_device_pixel_ratio(test_device_pixel_ratio)
    readonly property real reduced_resize_border:
        Math.max(
            0,
            VNM_chrome_geometry.snapped_logical_edge(6, test_device_pixel_ratio)
                - 2 / VNM_chrome_geometry.normalized_device_pixel_ratio(test_device_pixel_ratio))
    readonly property real reduced_titlebar_height:
        Math.max(
            0,
            VNM_chrome_geometry.snapped_logical_edge(32, test_device_pixel_ratio)
                - 2 / VNM_chrome_geometry.normalized_device_pixel_ratio(test_device_pixel_ratio))

    VNM_ChromeTheme {
        id: terminal_theme

        titlebar: "#12171e"
        titlebar_text: "#ebeff5"
        titlebar_button_icon: "#e2e8f0"
        titlebar_button_hover: "#272f3a"
        titlebar_button_pressed: "#343d4a"
        titlebar_close_hover: "#c6303a"
        titlebar_close_pressed: "#96222a"
        titlebar_activity_marker: "#71b4ff"
        titlebar_content_border: "transparent"
        window_frame_border: "#2a313c"
    }

    VNM_ChromeFrameShell {
        objectName: "terminal_like_frame_shell"
        anchors.fill: parent
        theme: terminal_theme
        frame_color: "#12171e"
        frame_outer_edge: root.frame_edge
        frame_outer_edge_color: "#2a313c"
        frame_gap: Math.max(0, root.reduced_resize_border - root.frame_edge)
        frame_inner_edge: root.frame_edge
        frame_inner_edge_color: "#2a313c"
        resize_target_extent: VNM_chrome_geometry.default_resize_target_extent
        device_pixel_ratio: root.test_device_pixel_ratio
        titlebar_height: Math.min(root.reduced_titlebar_height, root.height)
        active: true
        resize_enabled: true
    }
}
)";

        struct render_case_t {
            int   width;
            int   height;
            qreal device_pixel_ratio;
        };

        const render_case_t cases[] = {
            {201, 121, 1.0 },
            {492, 418, 1.0 },
            {493, 419, 1.25},
            {554, 488, 1.25},
        };

        const QColor expected_edge_color(QStringLiteral("#2a313c"));
        for (const render_case_t& c : cases) {
            QQuickWindow window;
            window.setColor(QColor(QStringLiteral("#000000")));
            window.resize(c.width, c.height);

            std::unique_ptr<QObject> root_object = create_qml_object(
                engine,
                qml_source,
                "qrc:/tests/frame_shell_terminal_chrome_pixel_contract.qml");
            QVERIFY(root_object != nullptr);

            auto* root_item = qobject_cast<QQuickItem*>(root_object.get());
            QVERIFY(root_item != nullptr);
            root_item->setParentItem(window.contentItem());
            root_item->setSize(QSizeF(c.width, c.height));
            QVERIFY(root_item->setProperty("test_device_pixel_ratio", c.device_pixel_ratio));

            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QTest::qWait(50);

            QImage image = window.grabWindow();
            QVERIFY(!image.isNull());
            QVERIFY(image.width() > 0);
            QVERIFY(image.height() > 0);

            const int right_x  = image.width() - 1;
            const int bottom_y = image.height() - 1;
            for (int y = 0; y < image.height(); ++y) {
                const QColor actual = image.pixelColor(right_x, y);
                const QByteArray message = QStringLiteral(
                    "right outer edge pixel mismatch at size %1x%2 dpr %3: "
                    "x=%4 y=%5 actual=%6 expected=%7")
                    .arg(c.width)
                    .arg(c.height)
                    .arg(c.device_pixel_ratio)
                    .arg(right_x)
                    .arg(y)
                    .arg(actual.name(QColor::HexArgb))
                    .arg(expected_edge_color.name(QColor::HexArgb))
                    .toUtf8();
                QVERIFY2(
                    color_nearly_equal(actual, expected_edge_color),
                    message.constData());
            }

            for (int x = 0; x < image.width(); ++x) {
                const QColor actual = image.pixelColor(x, bottom_y);
                const QByteArray message = QStringLiteral(
                    "bottom outer edge pixel mismatch at size %1x%2 dpr %3: "
                    "x=%4 y=%5 actual=%6 expected=%7")
                    .arg(c.width)
                    .arg(c.height)
                    .arg(c.device_pixel_ratio)
                    .arg(x)
                    .arg(bottom_y)
                    .arg(actual.name(QColor::HexArgb))
                    .arg(expected_edge_color.name(QColor::HexArgb))
                    .toUtf8();
                QVERIFY2(
                    color_nearly_equal(actual, expected_edge_color),
                    message.constData());
            }
        }
    }

    void titlebar_window_frame_top_edge_is_opt_in_and_above_button_backgrounds()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    width: 500
    height: 60

    VNM_ChromeTheme {
        id: custom_theme
        window_frame_border: "#556677"
    }

    VNM_ChromeTitleBar {
        objectName: "chrome_titlebar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 32
        theme: custom_theme
        window_frame_top_visible: true
        window_frame_width: 2
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/titlebar_window_frame_top_contract.qml");
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        auto* titlebar = qobject_cast<QQuickItem*>(
            find_descendant(root.get(), QStringLiteral("chrome_titlebar")));
        QVERIFY(titlebar != nullptr);
        auto* top = qobject_cast<QQuickItem*>(
            find_descendant(root.get(), QStringLiteral("titlebar_window_frame_top")));
        QVERIFY(top != nullptr);

        QVERIFY(top->isVisible());
        QCOMPARE(top->isEnabled(), false);
        QCOMPARE(top->z(), 3.0);
        QVERIFY(rect_nearly_equal(item_rect(*top), QRectF(0.0, 0.0, 500.0, 2.0)));
        QCOMPARE(object_color(top, "color"), QColor(QStringLiteral("#556677")));

        auto* mark = qobject_cast<QQuickItem*>(
            find_descendant(root.get(), QStringLiteral("vnm_animated_mark")));
        QVERIFY(mark != nullptr);
        QQuickItem* content_layer = mark->parentItem();
        QVERIFY(content_layer != nullptr);
        QCOMPARE(content_layer->parentItem(), titlebar);
        QVERIFY(top->z() > content_layer->z());

        QVERIFY(titlebar->setProperty("window_frame_top_visible", false));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY(!top->isVisible());
    }

    void titlebar_creates_visible_default_mark_and_propagates_theme_override()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import QtQuick.Controls
import VNM_Chrome

ApplicationWindow {
    width: 500
    height: 60
    visible: false
    font.family: "Inherited Chrome Font"
    font.styleName: "Inherited Chrome Style"
    font.weight: Font.DemiBold
    font.hintingPreference: Font.PreferFullHinting
    font.capitalization: Font.SmallCaps
    font.kerning: false
    font.letterSpacing: 1.25

    Label {
        objectName: "reference_title_label"
        visible: false
        font.pointSize: 9.5
    }

    TextInput {
        objectName: "reference_title_editor"
        visible: false
        font.pointSize: 9.5
    }

    VNM_ChromeTheme {
        id: custom_theme
        objectName: "custom_theme"
        titlebar: "#102030"
        titlebar_text: "#ddeeff"
    }

    VNM_ChromeTitleBar {
        objectName: "chrome_titlebar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        theme: custom_theme
        title: "Theme"
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/titlebar_default_mark_contract.qml");
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        auto* titlebar = qobject_cast<QQuickItem*>(
            find_descendant(root.get(), QStringLiteral("chrome_titlebar")));
        QVERIFY(titlebar != nullptr);
        QCOMPARE(object_color(titlebar, "color"), QColor(QStringLiteral("#102030")));

        QObject* title_label = find_descendant(root.get(), QStringLiteral("title_label"));
        QObject* title_editor = find_descendant(root.get(), QStringLiteral("title_editor"));
        QObject* reference_title_label =
            find_descendant(root.get(), QStringLiteral("reference_title_label"));
        QObject* reference_title_editor =
            find_descendant(root.get(), QStringLiteral("reference_title_editor"));
        QVERIFY(title_label != nullptr);
        QVERIFY(title_editor != nullptr);
        QVERIFY(reference_title_label != nullptr);
        QVERIFY(reference_title_editor != nullptr);

        const auto object_font = [](QObject* object) {
            return object->property("font").value<QFont>();
        };
        compare_font_contract(
            object_font(title_label), object_font(reference_title_label));
        compare_font_contract(
            object_font(title_editor), object_font(reference_title_editor));

        QVERIFY(titlebar->setProperty(
            "title_font_family",
            QStringLiteral("Caller Supplied Chrome Font")));
        QFont expected_title_label_font = object_font(reference_title_label);
        QFont expected_title_editor_font = object_font(reference_title_editor);
        expected_title_label_font.setFamily(QStringLiteral("Caller Supplied Chrome Font"));
        expected_title_editor_font.setFamily(QStringLiteral("Caller Supplied Chrome Font"));
        compare_font_contract(object_font(title_label), expected_title_label_font);
        compare_font_contract(object_font(title_editor), expected_title_editor_font);

        QVERIFY(titlebar->setProperty("title_font_family", QString()));
        compare_font_contract(
            object_font(title_label), object_font(reference_title_label));
        compare_font_contract(
            object_font(title_editor), object_font(reference_title_editor));

        QFont updated_window_font = root->property("font").value<QFont>();
        updated_window_font.setFamily(QStringLiteral("Updated Inherited Chrome Font"));
        updated_window_font.setWeight(QFont::ExtraBold);
        updated_window_font.setHintingPreference(QFont::PreferNoHinting);
        updated_window_font.setCapitalization(QFont::AllUppercase);
        updated_window_font.setKerning(true);
        updated_window_font.setLetterSpacing(QFont::AbsoluteSpacing, 2.5);
        QVERIFY(root->setProperty("font", updated_window_font));
        compare_font_contract(
            object_font(title_label), object_font(reference_title_label));
        compare_font_contract(
            object_font(title_editor), object_font(reference_title_editor));

        auto* mark = qobject_cast<QQuickItem*>(
            find_descendant(root.get(), QStringLiteral("vnm_animated_mark")));
        QVERIFY(mark != nullptr);
        QVERIFY(mark->isVisible());
        QCOMPARE(titlebar->property("mark_pid_reveal_enabled").toBool(), true);
        QCOMPARE(mark->property("pid_reveal_enabled").toBool(), true);
        QCOMPARE(titlebar->property("mark_stay_on_top_enabled").toBool(), true);
        QCOMPARE(mark->property("stay_on_top_enabled").toBool(), true);
        QCOMPARE(mark->property("stay_on_top_active").toBool(), false);
        QCOMPARE(
            titlebar->property("move_drag_threshold").toInt(),
            QGuiApplication::styleHints()->startDragDistance());
        QCOMPARE(
            mark->property("move_drag_threshold").toInt(),
            QGuiApplication::styleHints()->startDragDistance());
        QVERIFY(titlebar->setProperty("mark_pid_reveal_enabled", false));
        QCOMPARE(mark->property("pid_reveal_enabled").toBool(), false);
        QVERIFY(titlebar->setProperty("mark_stay_on_top_enabled", false));
        QCOMPARE(mark->property("stay_on_top_enabled").toBool(), false);

        QObject* custom_theme = find_descendant(root.get(), QStringLiteral("custom_theme"));
        QVERIFY(custom_theme != nullptr);
        QVERIFY(custom_theme->setProperty("titlebar", QColor(QStringLiteral("#405060"))));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QCOMPARE(object_color(titlebar, "color"), QColor(QStringLiteral("#405060")));
    }

    void titlebar_mark_tracks_initial_external_and_requested_topmost_flags()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import QtQuick.Window
import VNM_Chrome

Window {
    width: 500
    height: 60
    visible: false
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint

    VNM_ChromeTitleBar {
        objectName: "chrome_titlebar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/titlebar_topmost_contract.qml");
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        auto* window = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(window != nullptr);

        QObject* titlebar = find_descendant(
            root.get(), QStringLiteral("chrome_titlebar"));
        QObject* mark = find_descendant(
            root.get(), QStringLiteral("vnm_animated_mark"));
        QVERIFY(titlebar != nullptr);
        QVERIFY(mark     != nullptr);
        QVERIFY(window->flags().testFlag(Qt::WindowStaysOnTopHint));
        QCOMPARE(titlebar->property("window_stays_on_top").toBool(), true);
        QCOMPARE(mark->property("stay_on_top_active").toBool(), true);

        window->setFlag(Qt::WindowStaysOnTopHint, false);
        QTRY_VERIFY_WITH_TIMEOUT(
            !window->flags().testFlag(Qt::WindowStaysOnTopHint),
            1000);
        QCOMPARE(titlebar->property("window_stays_on_top").toBool(), false);
        QCOMPARE(mark->property("stay_on_top_active").toBool(), false);
        QVERIFY(window->flags().testFlag(Qt::FramelessWindowHint));

        QVERIFY(QMetaObject::invokeMethod(
            mark,
            "stay_on_top_change_requested",
            Q_ARG(bool, true)));
        QTRY_VERIFY_WITH_TIMEOUT(
            window->flags().testFlag(Qt::WindowStaysOnTopHint),
            1000);
        QCOMPARE(titlebar->property("window_stays_on_top").toBool(), true);
        QCOMPARE(mark->property("stay_on_top_active").toBool(), true);
        QVERIFY(window->flags().testFlag(Qt::FramelessWindowHint));

        window->setFlag(Qt::WindowStaysOnTopHint, false);
        QTRY_VERIFY_WITH_TIMEOUT(
            !window->flags().testFlag(Qt::WindowStaysOnTopHint),
            1000);
        QCOMPARE(titlebar->property("window_stays_on_top").toBool(), false);
        QCOMPARE(mark->property("stay_on_top_active").toBool(), false);

        window->setFlag(Qt::WindowStaysOnTopHint, true);
        QTRY_VERIFY_WITH_TIMEOUT(
            window->flags().testFlag(Qt::WindowStaysOnTopHint),
            1000);
        QCOMPARE(titlebar->property("window_stays_on_top").toBool(), true);
        QCOMPARE(mark->property("stay_on_top_active").toBool(), true);

        QVERIFY(QMetaObject::invokeMethod(
            mark,
            "stay_on_top_change_requested",
            Q_ARG(bool, false)));
        QTRY_VERIFY_WITH_TIMEOUT(
            !window->flags().testFlag(Qt::WindowStaysOnTopHint),
            1000);
        QCOMPARE(titlebar->property("window_stays_on_top").toBool(), false);
        QCOMPARE(mark->property("stay_on_top_active").toBool(), false);
        QVERIFY(window->flags().testFlag(Qt::FramelessWindowHint));
    }

    void inactive_titlebar_does_not_dim_child_opacity()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    width: 500
    height: 60

    VNM_ChromeTitleBar {
        objectName: "chrome_titlebar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        active: false
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/inactive_titlebar_opacity_contract.qml");
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        auto* titlebar = qobject_cast<QQuickItem*>(
            find_descendant(root.get(), QStringLiteral("chrome_titlebar")));
        QVERIFY(titlebar != nullptr);
        QCOMPARE(titlebar->opacity(), 1.0);

        auto* mark = qobject_cast<QQuickItem*>(
            find_descendant(root.get(), QStringLiteral("vnm_animated_mark")));
        QVERIFY(mark != nullptr);
        QCOMPARE(mark->opacity(), 1.0);

        auto* title_label = qobject_cast<QQuickItem*>(
            find_descendant(root.get(), QStringLiteral("title_label")));
        QVERIFY(title_label != nullptr);
        QCOMPARE(title_label->opacity(), 1.0);
    }

    void titlebar_snaps_content_inset_at_fractional_dpr()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    width: 500
    height: 60

    VNM_ChromeTitleBar {
        objectName: "chrome_titlebar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 32
        resize_target_extent: 11
        device_pixel_ratio: 1.25
    }
}
)";

        std::unique_ptr<QObject> root_object = create_qml_object(
            engine, qml_source, "qrc:/tests/titlebar_fractional_dpr_contract.qml");
        QVERIFY(root_object != nullptr);
        auto* root = qobject_cast<QQuickItem*>(root_object.get());
        QVERIFY(root != nullptr);
        root->ensurePolished();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        auto* titlebar = qobject_cast<QQuickItem*>(
            find_descendant(root_object.get(), QStringLiteral("chrome_titlebar")));
        QVERIFY(titlebar != nullptr);
        QVERIFY(nearly_equal(titlebar->property("content_border_width").toReal(), 0.8));

        auto* mark = qobject_cast<QQuickItem*>(
            find_descendant(root_object.get(), QStringLiteral("vnm_animated_mark")));
        QVERIFY(mark != nullptr);
        const QPointF mark_origin = mark->mapToItem(root, QPointF(0.0, 0.0));
        QVERIFY(nearly_equal(mark_origin.x(), 3.2));
        QVERIFY(vnm_qml_chrome::rect_has_snapped_physical_edges(
            QRectF(mark_origin.x(), 0.0, mark->width(), 0.0),
            1.25));

        QVERIFY(titlebar->setProperty("content_left_inset", 5.7));
        root->ensurePolished();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        const QPointF overridden_mark_origin =
            mark->mapToItem(root, QPointF(0.0, 0.0));
        QVERIFY(nearly_equal(overridden_mark_origin.x(), 5.6));
        QVERIFY(vnm_qml_chrome::rect_has_snapped_physical_edges(
            QRectF(
                overridden_mark_origin.x(),
                0.0,
                mark->width(),
                0.0),
            1.25));

        auto* top_left_resize_area = qobject_cast<QQuickItem*>(
            find_descendant(root_object.get(), QStringLiteral("top_left_resize_area")));
        QVERIFY(top_left_resize_area != nullptr);
        QVERIFY(nearly_equal(top_left_resize_area->width(),  11.2));
        QVERIFY(nearly_equal(top_left_resize_area->height(), 11.2));
    }

    void resize_layers_preserve_fractional_resize_target_extents()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    width: 240
    height: 140

    VNM_ChromeSideResizeLayer {
        objectName: "side_layer"
        anchors.fill: parent
        resize_target_extent: 5.6
    }

    VNM_ChromeBottomResizeLayer {
        objectName: "bottom_layer"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: implicitHeight
        resize_target_extent: 5.6
    }

    VNM_ChromeTitleBar {
        objectName: "chrome_titlebar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 32
        resize_target_extent: 5.6
        device_pixel_ratio: 1.25
    }
}
)";

        std::unique_ptr<QObject> root_object = create_qml_object(
            engine,
            qml_source,
            "qrc:/tests/fractional_resize_target_extent_contract.qml");
        QVERIFY(root_object != nullptr);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        auto* left_resize_area = qobject_cast<QQuickItem*>(
            find_descendant(root_object.get(), QStringLiteral("left_resize_area")));
        QVERIFY(left_resize_area != nullptr);
        QVERIFY(nearly_equal(left_resize_area->width(), 5.6));

        auto* bottom_layer = qobject_cast<QQuickItem*>(
            find_descendant(root_object.get(), QStringLiteral("bottom_layer")));
        QVERIFY(bottom_layer != nullptr);
        QVERIFY(nearly_equal(bottom_layer->height(), 5.6));

        auto* bottom_left_resize_area = qobject_cast<QQuickItem*>(
            find_descendant(root_object.get(), QStringLiteral("bottom_left_resize_area")));
        QVERIFY(bottom_left_resize_area != nullptr);
        QVERIFY(nearly_equal(bottom_left_resize_area->width(),  5.6));
        QVERIFY(nearly_equal(bottom_left_resize_area->height(), 5.6));

        auto* top_left_resize_area = qobject_cast<QQuickItem*>(
            find_descendant(root_object.get(), QStringLiteral("top_left_resize_area")));
        QVERIFY(top_left_resize_area != nullptr);
        QVERIFY(nearly_equal(top_left_resize_area->width(),  5.6));
        QVERIFY(nearly_equal(top_left_resize_area->height(), 5.6));
    }

    void titlebar_activity_marker_is_optional_and_themeable()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    width: 500
    height: 60

    VNM_ChromeTheme {
        id: custom_theme
        titlebar_activity_marker: "#112233"
    }

    VNM_ChromeTitleBar {
        objectName: "chrome_titlebar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        theme: custom_theme
        title: "Build"
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/titlebar_activity_marker_contract.qml");
        QVERIFY(root != nullptr);
        auto* root_item = qobject_cast<QQuickItem*>(root.get());
        QVERIFY(root_item != nullptr);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        QObject* titlebar = find_descendant(root.get(), QStringLiteral("chrome_titlebar"));
        QVERIFY(titlebar != nullptr);
        auto* marker_label = qobject_cast<QQuickItem*>(
            find_descendant(root.get(), QStringLiteral("activity_marker_label")));
        QVERIFY(marker_label != nullptr);
        auto* title_label = qobject_cast<QQuickItem*>(
            find_descendant(root.get(), QStringLiteral("title_label")));
        QVERIFY(title_label != nullptr);
        QVERIFY(!marker_label->isVisible());

        const QString marker_text(QChar(0x2731));
        QVERIFY(titlebar->setProperty("activity_marker_text", marker_text));
        root_item->ensurePolished();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        QVERIFY(marker_label->isVisible());
        QCOMPARE(marker_label->property("text").toString(), marker_text);
        QCOMPARE(object_color(marker_label, "color"), QColor(QStringLiteral("#112233")));
        const qreal title_x_with_marker = title_label->x();

        QVERIFY(titlebar->setProperty("activity_marker_text", QString()));
        root_item->ensurePolished();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        QVERIFY(marker_label->isVisible());
        QCOMPARE(marker_label->property("text").toString(), QString());
        QVERIFY(nearly_equal(title_label->x(), title_x_with_marker));
    }

    void titlebar_window_buttons_are_individually_optional()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    width: 500
    height: 60

    VNM_ChromeTitleBar {
        objectName: "chrome_titlebar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        title: "Dialog"
        minimize_button_visible: false
        maximize_button_visible: false
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/titlebar_window_buttons_contract.qml");
        QVERIFY(root != nullptr);
        auto* root_item = qobject_cast<QQuickItem*>(root.get());
        QVERIFY(root_item != nullptr);
        root_item->ensurePolished();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        QObject* titlebar = find_descendant(root.get(), QStringLiteral("chrome_titlebar"));
        QVERIFY(titlebar != nullptr);

        auto* minimize_button = qobject_cast<QQuickItem*>(
            find_descendant(root.get(), QStringLiteral("minimize_button")));
        auto* maximize_button = qobject_cast<QQuickItem*>(
            find_descendant(root.get(), QStringLiteral("maximize_button")));
        auto* close_button = qobject_cast<QQuickItem*>(
            find_descendant(root.get(), QStringLiteral("close_button")));
        auto* button_row = qobject_cast<QQuickItem*>(
            find_descendant(root.get(), QStringLiteral("titlebar_buttons")));
        QVERIFY(minimize_button != nullptr);
        QVERIFY(maximize_button != nullptr);
        QVERIFY(close_button != nullptr);
        QVERIFY(button_row != nullptr);

        QVERIFY(!minimize_button->isVisible());
        QVERIFY(!maximize_button->isVisible());
        QVERIFY(close_button->isVisible());
        QVERIFY(nearly_equal(button_row->width(), 46.0));

        // The buttons react to the property, so re-showing one restores it.
        QVERIFY(titlebar->setProperty("minimize_button_visible", true));
        root_item->ensurePolished();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY(minimize_button->isVisible());
    }

    void titlebar_custom_buttons_place_and_invoke_action()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    width: 500
    height: 60
    property int activations: 0

    VNM_ChromeTitleBar {
        objectName: "chrome_titlebar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        title: "Custom"
        custom_buttons: [{
            object_name: "probe_button",
            width: 40,
            tooltip: "Probe",
            action: function() { activations += 1 }
        }]
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/titlebar_custom_buttons_contract.qml");
        QVERIFY(root != nullptr);
        auto* root_item = qobject_cast<QQuickItem*>(root.get());
        QVERIFY(root_item != nullptr);
        root_item->ensurePolished();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        QObject* titlebar = find_descendant(root.get(), QStringLiteral("chrome_titlebar"));
        QVERIFY(titlebar != nullptr);
        QVERIFY(has_property(titlebar, "custom_buttons"));

        auto* probe = qobject_cast<QQuickItem*>(
            find_descendant(root.get(), QStringLiteral("probe_button")));
        QVERIFY(probe != nullptr);
        QVERIFY(probe->isVisible());
        QVERIFY(nearly_equal(probe->width(), 40.0));
        QVERIFY(probe->height() > 0.0);

        // The custom button sits to the left of the window controls, flush.
        auto* button_row = qobject_cast<QQuickItem*>(
            find_descendant(root.get(), QStringLiteral("titlebar_buttons")));
        QVERIFY(button_row != nullptr);
        const qreal probe_right =
            probe->mapToItem(root_item, QPointF(probe->width(), 0.0)).x();
        const qreal buttons_left =
            button_row->mapToItem(root_item, QPointF(0.0, 0.0)).x();
        QVERIFY2(nearly_equal(buttons_left - probe_right, 0.0),
            "Custom button sits flush against the window controls.");

        QCOMPARE(root->property("activations").toInt(), 0);
        QVERIFY(QMetaObject::invokeMethod(probe, "clicked"));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QCOMPARE(root->property("activations").toInt(), 1);
    }

    void titlebar_custom_buttons_load_vector_component()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    width: 500
    height: 60

    Component {
        id: vector_icon

        Item {
            objectName: "probe_vector_icon"
            anchors.centerIn: parent
            width: 16
            height: 16
        }
    }

    VNM_ChromeTitleBar {
        objectName: "chrome_titlebar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        title: "Custom"
        custom_buttons: [{
            object_name: "probe_button",
            component: vector_icon,
            width: 40
        }]
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/titlebar_custom_button_component_contract.qml");
        QVERIFY(root != nullptr);
        auto* root_item = qobject_cast<QQuickItem*>(root.get());
        QVERIFY(root_item != nullptr);
        root_item->ensurePolished();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        auto* probe = qobject_cast<QQuickItem*>(
            find_descendant(root.get(), QStringLiteral("probe_button")));
        QVERIFY(probe != nullptr);

        // The component is instantiated inside the button, so an app can draw
        // an icon that does not depend on the fonts the host system installed.
        auto* icon = qobject_cast<QQuickItem*>(
            find_descendant(probe, QStringLiteral("probe_vector_icon")));
        QVERIFY(icon != nullptr);
        QVERIFY(icon->isVisible());

        // The slot fills the button, so centred content lands on the button's
        // own centre rather than on a zero-sized loader in its corner.
        const QPointF icon_centre = icon->mapToItem(
            probe, QPointF(icon->width() / 2.0, icon->height() / 2.0));
        QVERIFY(nearly_equal(icon_centre.x(), probe->width() / 2.0));
        QVERIFY(nearly_equal(icon_centre.y(), probe->height() / 2.0));
    }

    void titlebar_title_has_margin_after_mark_without_actions()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    width: 500
    height: 60

    VNM_ChromeTitleBar {
        objectName: "chrome_titlebar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        title: "Logonomic"
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/titlebar_title_margin_contract.qml");
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        auto* animated_mark = qobject_cast<QQuickItem*>(
            find_descendant(root.get(), QStringLiteral("vnm_animated_mark")));
        auto* title_label = qobject_cast<QQuickItem*>(
            find_descendant(root.get(), QStringLiteral("title_label")));
        QVERIFY(animated_mark != nullptr);
        QVERIFY(title_label != nullptr);

        const qreal title_gap = title_label->x()
            - (animated_mark->x() + animated_mark->width());
        QVERIFY2(title_gap >= 7.5,
            "Title label must not sit directly against the animated mark.");
    }

    void animated_mark_supports_normal_hover_state_transition()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

VNM_AnimatedMark {
    objectName: "animated_mark"
    mark_size: 20
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/animated_mark_transition_contract.qml");
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        QObject* rotor = find_descendant(
            root.get(), QStringLiteral("vnm_mark_rotor"));
        QVERIFY(rotor != nullptr);
        QCOMPARE(root->property("state").toString(), QString());
        QVERIFY(root->setProperty("hover_active", true));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QCOMPARE(root->property("state").toString(), QStringLiteral("normal_hover"));

        QVERIFY(root->setProperty("alt_reveal_forced", true));
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(rotor->property("rotation").toReal() - 45.0) < 0.01,
            500);
    }

    void animated_mark_plain_release_preserves_morph_through_synchronous_feedback()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    id: root

    width: 40
    height: 40
    property int topmost_request_count: 0
    property int effective_feedback_count: 0
    property bool last_topmost_request: false

    VNM_AnimatedMark {
        id: mark
        objectName: "animated_mark"
        anchors.centerIn: parent
        mark_size: 20

        onStay_on_top_change_requested: (requested_active) => {
            root.topmost_request_count += 1
            root.last_topmost_request = requested_active
            mark.stay_on_top_active = requested_active
            root.effective_feedback_count += 1
        }
    }

    QtObject {
        id: mouse_probe

        property real x: 10
        property real y: 10
        property int button: Qt.LeftButton
        property int buttons: Qt.NoButton
        property int modifiers: Qt.NoModifier
        property bool accepted: false
    }

    function plain_click() {
        mouse_probe.buttons = Qt.LeftButton
        mouse_probe.accepted = false
        mark.handle_primary_press(mouse_probe)
        mouse_probe.buttons = Qt.NoButton
        mark.handle_primary_release(mouse_probe)
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine,
            qml_source,
            "qrc:/tests/animated_mark_immediate_click_morph_contract.qml");
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        QObject* mark = find_descendant(
            root.get(), QStringLiteral("animated_mark"));
        QObject* grey = find_descendant(
            root.get(), QStringLiteral("vnm_mark_grey"));
        QObject* orange = find_descendant(
            root.get(), QStringLiteral("vnm_mark_orange"));
        QObject* eye = find_descendant(
            root.get(), QStringLiteral("vnm_mark_stay_on_top_eye"));
        QVERIFY(mark   != nullptr);
        QVERIFY(grey   != nullptr);
        QVERIFY(orange != nullptr);
        QVERIFY(eye    != nullptr);

        QVERIFY(mark->setProperty("hover_active", true));
        QTest::qWait(45);
        const qreal orange_target_scale =
            mark->property("orange_scale").toReal();
        QVERIFY(orange->property("scale").toReal() > 1.0);
        QVERIFY(orange->property("scale").toReal() < orange_target_scale);
        QCOMPARE(mark->property("state").toString(), QStringLiteral("normal_hover"));

        QVERIFY(QMetaObject::invokeMethod(root.get(), "plain_click"));
        QCOMPARE(root->property("topmost_request_count").toInt(), 1);
        QCOMPARE(root->property("effective_feedback_count").toInt(), 1);
        QCOMPARE(root->property("last_topmost_request").toBool(), true);
        QCOMPARE(mark->property("stay_on_top_active").toBool(), true);
        QCOMPARE(mark->property("state").toString(), QStringLiteral("normal_hover"));
        QVERIFY(mark->setProperty("hover_active", false));

        qreal previous_scale = orange->property("scale").toReal();
        qreal previous_radius = orange->property("radius").toReal();
        qreal previous_offset = orange->property("circle_x_offset").toReal();
        qreal previous_grey_opacity = grey->property("opacity").toReal();
        auto verify_forward_geometry = [&]() {
            QCOMPARE(
                mark->property("state").toString(),
                QStringLiteral("normal_hover"));

            const qreal scale = orange->property("scale").toReal();
            const qreal radius = orange->property("radius").toReal();
            const qreal offset = orange->property("circle_x_offset").toReal();
            const qreal grey_opacity = grey->property("opacity").toReal();
            QVERIFY2(scale + 0.02 >= previous_scale,
                "Synchronous topmost feedback reversed the orange scale morph.");
            QVERIFY2(radius + 0.02 >= previous_radius,
                "Synchronous topmost feedback reversed the orange radius morph.");
            QVERIFY2(offset + 0.02 >= previous_offset,
                "Synchronous topmost feedback reversed the orange offset morph.");
            QVERIFY2(grey_opacity <= previous_grey_opacity + 0.02,
                "Synchronous topmost feedback made the grey mark reappear.");
            previous_scale = scale;
            previous_radius = radius;
            previous_offset = offset;
            previous_grey_opacity = grey_opacity;
        };

        for (int sample = 0; sample < 80; ++sample) {
            verify_forward_geometry();
            QTest::qWait(5);
        }

        QCOMPARE(root->property("topmost_request_count").toInt(), 1);
        QVERIFY(nearly_equal(
            orange->property("scale").toReal(), orange_target_scale));
        QVERIFY(qAbs(grey->property("opacity").toReal()) < 0.01);
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(eye->property("opacity").toReal() - 1.0) < 0.01,
            1000);
    }

    void animated_mark_plain_click_toggles_eye_ctrl_click_reveals_pid()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    id: root

    width: 40
    height: 40
    property int topmost_request_count: 0
    property bool last_topmost_request: false
    property int move_request_count: 0

    VNM_AnimatedMark {
        id: mark
        objectName: "animated_mark"
        anchors.centerIn: parent
        mark_size: 20
        move_enabled: true

        onStay_on_top_change_requested: (requested_active) => {
            root.topmost_request_count += 1
            root.last_topmost_request = requested_active
            mark.stay_on_top_active = requested_active
        }
        onMove_requested: root.move_request_count += 1
    }

    QtObject {
        id: mouse_probe

        property real x: 0
        property real y: 0
        property int button: Qt.LeftButton
        property int buttons: Qt.NoButton
        property int modifiers: Qt.NoModifier
        property bool accepted: false
    }

    function prepare_mouse(modifiers, x, y, buttons) {
        mouse_probe.x = x
        mouse_probe.y = y
        mouse_probe.button = Qt.LeftButton
        mouse_probe.buttons = buttons
        mouse_probe.modifiers = modifiers
        mouse_probe.accepted = false
    }

    function plain_click() {
        prepare_mouse(Qt.NoModifier, 10, 10, Qt.LeftButton)
        mark.handle_primary_press(mouse_probe)
        mouse_probe.buttons = Qt.NoButton
        mark.handle_primary_release(mouse_probe)
    }

    function release_outside_does_not_toggle() {
        const requests_before = root.topmost_request_count
        prepare_mouse(Qt.NoModifier, 10, 10, Qt.LeftButton)
        mark.handle_primary_press(mouse_probe)
        mouse_probe.x = mark.width + 1
        mouse_probe.buttons = Qt.NoButton
        mark.handle_primary_release(mouse_probe)
        return root.topmost_request_count === requests_before
    }

    function ctrl_click_starts_pid_only() {
        const requests_before = root.topmost_request_count
        prepare_mouse(Qt.ControlModifier, 10, 10, Qt.LeftButton)
        mark.handle_primary_press(mouse_probe)
        const pid_started = mark.pid_phase === "forming"
        mouse_probe.buttons = Qt.NoButton
        mark.handle_primary_release(mouse_probe)
        const no_topmost_request =
            root.topmost_request_count === requests_before
        mark.cancel_pid_reveal()
        return pid_started && no_topmost_request
    }

    function ctrl_click() {
        prepare_mouse(Qt.ControlModifier, 10, 10, Qt.LeftButton)
        mark.handle_primary_press(mouse_probe)
        mouse_probe.buttons = Qt.NoButton
        mark.handle_primary_release(mouse_probe)
    }

    function pill_press(modifiers) {
        prepare_mouse(modifiers, 10, 10, Qt.LeftButton)
        mark.handle_pill_press(mouse_probe)
        return mouse_probe.accepted
    }

    function below_threshold_motion_does_not_move() {
        const moves_before = root.move_request_count
        prepare_mouse(Qt.NoModifier, 0, 0, Qt.LeftButton)
        mark.handle_primary_press(mouse_probe)
        mouse_probe.accepted = false
        mouse_probe.x = mark.move_drag_threshold - 0.25
        mark.maybe_start_system_move(mouse_probe)
        const result = root.move_request_count === moves_before
            && !mouse_probe.accepted
        mark.cancel_primary_press()
        return result
    }

    function threshold_drag_does_not_toggle() {
        const requests_before = root.topmost_request_count
        const moves_before = root.move_request_count
        prepare_mouse(Qt.NoModifier, 0, 0, Qt.LeftButton)
        mark.handle_primary_press(mouse_probe)
        mouse_probe.x = mark.move_drag_threshold
        mark.maybe_start_system_move(mouse_probe)
        mouse_probe.buttons = Qt.NoButton
        mark.handle_primary_release(mouse_probe)
        return root.topmost_request_count === requests_before
            && root.move_request_count === moves_before + 1
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/animated_mark_click_contract.qml");
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        QObject* mark = find_descendant(
            root.get(), QStringLiteral("animated_mark"));
        QObject* eye = find_descendant(
            root.get(), QStringLiteral("vnm_mark_stay_on_top_eye"));
        QVERIFY(mark != nullptr);
        QVERIFY(eye  != nullptr);
        QCOMPARE(eye->property("source").toUrl().fileName(),
            QStringLiteral("vnm_mark_eye.svg"));
        QCOMPARE(eye->property("status").toInt(), 1);
        QVERIFY(mark->property("move_drag_threshold").toReal() > 0);

        QVERIFY(QMetaObject::invokeMethod(root.get(), "plain_click"));
        QCOMPARE(root->property("topmost_request_count").toInt(), 1);
        QCOMPARE(root->property("last_topmost_request").toBool(), true);
        QCOMPARE(mark->property("stay_on_top_active").toBool(), true);
        QCOMPARE(mark->property("pid_phase").toString(), QString());
        QCOMPARE(mark->property("state").toString(), QStringLiteral("normal_hover"));
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(eye->property("opacity").toReal() - 1.0) < 0.01,
            1000);

        QObject* rotor = find_descendant(
            root.get(), QStringLiteral("vnm_mark_rotor"));
        QVERIFY(rotor != nullptr);
        QVERIFY(mark->setProperty("alt_reveal_forced", true));
        QTest::qWait(300);
        QVERIFY(qAbs(rotor->property("rotation").toReal()) < 0.01);
        QVERIFY(mark->setProperty("alt_reveal_forced", false));

        QVERIFY(QMetaObject::invokeMethod(root.get(), "plain_click"));
        QCOMPARE(root->property("topmost_request_count").toInt(), 2);
        QCOMPARE(root->property("last_topmost_request").toBool(), false);
        QCOMPARE(mark->property("stay_on_top_active").toBool(), false);
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(eye->property("opacity").toReal()) < 0.01,
            1000);

        QVariant release_outside_result;
        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "release_outside_does_not_toggle",
            Q_RETURN_ARG(QVariant, release_outside_result)));
        QVERIFY(release_outside_result.toBool());
        QCOMPARE(root->property("topmost_request_count").toInt(), 2);

        QVariant ctrl_click_result;
        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "ctrl_click_starts_pid_only",
            Q_RETURN_ARG(QVariant, ctrl_click_result)));
        QVERIFY(ctrl_click_result.toBool());
        QCOMPARE(root->property("topmost_request_count").toInt(), 2);

        QVariant below_threshold_result;
        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "below_threshold_motion_does_not_move",
            Q_RETURN_ARG(QVariant, below_threshold_result)));
        QVERIFY(below_threshold_result.toBool());

        QVariant threshold_drag_result;
        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "threshold_drag_does_not_toggle",
            Q_RETURN_ARG(QVariant, threshold_drag_result)));
        QVERIFY(threshold_drag_result.toBool());
        QCOMPARE(root->property("topmost_request_count").toInt(), 2);

        QVERIFY(mark->setProperty("hover_active", true));
        QVERIFY(QMetaObject::invokeMethod(mark, "request_pid_reveal"));
        QTRY_VERIFY_WITH_TIMEOUT(
            mark->property("pid_phase").toString() == QStringLiteral("revealed"),
            5000);
        QVERIFY(QMetaObject::invokeMethod(root.get(), "ctrl_click"));
        QCOMPARE(
            mark->property("pid_phase").toString(),
            QStringLiteral("retracting"));
        QTRY_VERIFY_WITH_TIMEOUT(
            mark->property("pid_phase").toString().isEmpty(),
            5000);

        QVERIFY(QMetaObject::invokeMethod(mark, "request_pid_reveal"));
        QTRY_VERIFY_WITH_TIMEOUT(
            mark->property("pid_phase").toString() == QStringLiteral("revealed"),
            5000);

        // A plain press on the pill is refused so the PID text stays
        // selectable; Ctrl+press retracts from anywhere on the pill.
        QVariant pill_plain_accepted;
        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "pill_press",
            Q_RETURN_ARG(QVariant, pill_plain_accepted),
            Q_ARG(QVariant, QVariant(int(Qt::NoModifier)))));
        QVERIFY(!pill_plain_accepted.toBool());
        QCOMPARE(
            mark->property("pid_phase").toString(),
            QStringLiteral("revealed"));

        QVariant pill_ctrl_accepted;
        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "pill_press",
            Q_RETURN_ARG(QVariant, pill_ctrl_accepted),
            Q_ARG(QVariant, QVariant(int(Qt::ControlModifier)))));
        QVERIFY(pill_ctrl_accepted.toBool());
        QCOMPARE(
            mark->property("pid_phase").toString(),
            QStringLiteral("retracting"));
        QTRY_VERIFY_WITH_TIMEOUT(
            mark->property("pid_phase").toString().isEmpty(),
            5000);
    }

    void titlebar_routes_double_clicks_by_hit_target()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import QtQuick.Window
import VNM_Chrome

Window {
    width: 320
    height: 60
    visible: true

    VNM_ChromeTitleBar {
        objectName: "chrome_titlebar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/titlebar_double_click_routing_contract.qml");
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        auto* window = qobject_cast<QQuickWindow*>(root.get());
        auto* titlebar = find_descendant(
            root.get(), QStringLiteral("chrome_titlebar"));
        auto* mark = find_item(
            root.get(), QStringLiteral("vnm_animated_mark"));
        QVERIFY(window   != nullptr);
        QVERIFY(titlebar != nullptr);
        QVERIFY(mark     != nullptr);
        QVERIFY(mark->property("move_enabled").toBool());
        QCOMPARE(
            mark->property("move_drag_threshold").toInt(),
            QGuiApplication::styleHints()->startDragDistance());

        QSignalSpy maximize_spy(titlebar, SIGNAL(maximize_toggle_requested()));
        QSignalSpy topmost_spy(mark, SIGNAL(stay_on_top_change_requested(bool)));
        QVERIFY(maximize_spy.isValid());
        QVERIFY(topmost_spy.isValid());

        const QPointF mark_center = mark->mapToScene(
            QPointF(mark->width() / 2.0, mark->height() / 2.0));
        QTest::mouseDClick(
            window,
            Qt::LeftButton,
            Qt::NoModifier,
            mark_center.toPoint());
        QCOMPARE(topmost_spy.count(), 1);
        QCOMPARE(maximize_spy.count(), 0);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QCOMPARE(topmost_spy.count(), 1);
        QCOMPARE(maximize_spy.count(), 0);

        const QPoint titlebar_background(
            int(window->width() / 2.0),
            int(qobject_cast<QQuickItem*>(titlebar)->height() / 2.0));
        QTest::mouseDClick(
            window,
            Qt::LeftButton,
            Qt::NoModifier,
            titlebar_background);
        QCOMPARE(maximize_spy.count(), 1);
        QCOMPARE(topmost_spy.count(), 1);
    }

    void animated_mark_eye_resource_is_a_transparent_white_path()
    {
        QFile eye_file(
            QStringLiteral(":/vnm_qml_chrome/qml/VNM_Chrome/vnm_mark_eye.svg"));
        QVERIFY(eye_file.open(QIODevice::ReadOnly));

        QXmlStreamReader svg(&eye_file);
        int svg_count  = 0;
        int path_count = 0;
        int rect_count = 0;
        QColor fill;
        QString stroke;
        while (!svg.atEnd()) {
            svg.readNext();
            if (!svg.isStartElement()) {
                continue;
            }

            const QXmlStreamAttributes attributes = svg.attributes();
            if (svg.name() == QStringLiteral("svg")) {
                ++svg_count;
                QVERIFY(!attributes.hasAttribute(QStringLiteral("fill")));
                QVERIFY(!attributes.hasAttribute(QStringLiteral("style")));
            }
            else if (svg.name() == QStringLiteral("path")) {
                ++path_count;
                fill = QColor(
                    attributes.value(QStringLiteral("fill")).toString());
                stroke = attributes.value(
                    QStringLiteral("stroke")).toString();
                QVERIFY(!attributes.hasAttribute(QStringLiteral("style")));
            }
            else if (svg.name() == QStringLiteral("rect")) {
                ++rect_count;
            }
        }

        QVERIFY2(!svg.hasError(), qPrintable(svg.errorString()));
        QCOMPARE(svg_count, 1);
        QCOMPARE(path_count, 1);
        QCOMPARE(rect_count, 0);
        QCOMPARE(fill, QColor(QStringLiteral("#ffffff")));
        QVERIFY(stroke.isEmpty() || stroke == QStringLiteral("none"));
    }

    void animated_mark_eye_twenty_pixel_render_stays_transparent_and_bright()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

VNM_AnimatedMark {
    objectName: "animated_mark"
    mark_size: 20
}
)";

        std::unique_ptr<QObject> root_object = create_qml_object(
            engine, qml_source, "qrc:/tests/animated_mark_eye_render_contract.qml");
        auto* mark = qobject_cast<QQuickItem*>(root_object.get());
        QVERIFY(mark != nullptr);

        auto* grey = find_item(root_object.get(), QStringLiteral("vnm_mark_grey"));
        auto* eye = find_item(
            root_object.get(), QStringLiteral("vnm_mark_stay_on_top_eye"));
        QVERIFY(grey != nullptr);
        QVERIFY(eye  != nullptr);

        QQuickWindow window;
        window.setColor(QColor(QStringLiteral("#315a7d")));
        window.resize(20, 20);
        mark->setParentItem(window.contentItem());
        QVERIFY(mark->setProperty("hover_active", true));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(grey->property("opacity").toReal()) < 0.01,
            1000);
        auto circle_settled = [mark]() {
            QVariant settled;
            return QMetaObject::invokeMethod(
                       mark,
                       "circle_settled",
                       Q_RETURN_ARG(QVariant, settled))
                && settled.toBool();
        };
        QTRY_VERIFY_WITH_TIMEOUT(circle_settled(), 1000);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        const QImage baseline = window.grabWindow();
        QVERIFY(!baseline.isNull());
        QCOMPARE(baseline.size(), QSize(20, 20));

        QVERIFY(mark->setProperty("stay_on_top_active", true));
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(eye->property("opacity").toReal() - 1.0) < 0.01,
            1000);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        const QImage with_eye = window.grabWindow();
        QVERIFY(!with_eye.isNull());
        QCOMPARE(with_eye.size(), baseline.size());

        constexpr int channel_tolerance = 3;
        auto pixels_nearly_equal = [](const QColor& lhs, const QColor& rhs) {
            return qAbs(lhs.red()   - rhs.red())   <= channel_tolerance
                && qAbs(lhs.green() - rhs.green()) <= channel_tolerance
                && qAbs(lhs.blue()  - rhs.blue())  <= channel_tolerance
                && qAbs(lhs.alpha() - rhs.alpha()) <= channel_tolerance;
        };

        // These are the corners of the centred 16x16 Image canvas. They must
        // expose the same underlying pixels rather than an opaque SVG matte.
        for (const QPoint& point : {
                 QPoint(2, 2), QPoint(17, 2), QPoint(2, 17), QPoint(17, 17)})
        {
            QVERIFY(pixels_nearly_equal(
                baseline.pixelColor(point), with_eye.pixelColor(point)));
        }

        int changed_pixel_count = 0;
        int near_white_count     = 0;
        for (int y = 0; y < baseline.height(); ++y) {
            for (int x = 0; x < baseline.width(); ++x) {
                const QColor before = baseline.pixelColor(x, y);
                const QColor after  = with_eye.pixelColor(x, y);
                if (pixels_nearly_equal(before, after)) {
                    continue;
                }

                ++changed_pixel_count;
                const QByteArray message = QStringLiteral(
                    "eye darkened its baseline at x=%1 y=%2: before=%3 after=%4")
                    .arg(x)
                    .arg(y)
                    .arg(before.name(QColor::HexArgb))
                    .arg(after.name(QColor::HexArgb))
                    .toUtf8();
                QVERIFY2(
                    after.red()   + channel_tolerance >= before.red()
                    && after.green() + channel_tolerance >= before.green()
                    && after.blue()  + channel_tolerance >= before.blue(),
                    message.constData());
                if (after.red() >= 235
                    && after.green() >= 235
                    && after.blue() >= 235)
                {
                    ++near_white_count;
                }
            }
        }

        QVERIFY(changed_pixel_count > 0);
        QVERIFY(near_white_count > 0);
    }

    void animated_mark_reveals_process_id_pill_on_request()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

VNM_AnimatedMark {
    objectName: "animated_mark"
    mark_size: 20
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/animated_mark_pid_reveal_contract.qml");
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        QCOMPARE(root->property("pid_reveal_enabled").toBool(), true);
        QCOMPARE(root->property("pid_phase").toString(), QString());
        QVERIFY(root->setProperty("hover_active", true));
        QVERIFY(QMetaObject::invokeMethod(root.get(), "request_pid_reveal"));
        QCOMPARE(root->property("pid_phase").toString(), QStringLiteral("forming"));

        QTRY_VERIFY_WITH_TIMEOUT(
            root->property("pid_phase").toString() == QStringLiteral("revealed"),
            5000);

        QObject* pill = find_descendant(
            root.get(), QStringLiteral("vnm_mark_pid_pill"));
        QVERIFY(pill != nullptr);
        QVERIFY(pill->property("visible").toBool());
        QVERIFY(pill->property("width").toReal() > 20.0);
        QVERIFY(root->property("pid_layout_width").toReal() > 20.0);

        QObject* pid_edit = find_descendant(
            root.get(), QStringLiteral("vnm_mark_pid_edit"));
        QVERIFY(pid_edit != nullptr);
        QCOMPARE(
            pid_edit->property("text").toString(),
            QString::number(QCoreApplication::applicationPid()));
        QVERIFY(pid_edit->property("selectedText").toString().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(pid_edit->property("opacity").toReal() - 1.0) < 0.01,
            1000);

        // The caption names the number without joining it: it stays a separate
        // item, so the editor still holds the bare process ID Ctrl+C copies.
        QObject* pid_caption = find_descendant(
            root.get(), QStringLiteral("vnm_mark_pid_caption"));
        QVERIFY(pid_caption != nullptr);
        QCOMPARE(
            pid_caption->property("text").toString(), QStringLiteral("PID:"));
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(pid_caption->property("opacity").toReal() - 1.0) < 0.01,
            1000);

        QVERIFY(root->setProperty("mark_size", 80.0));
        QTRY_VERIFY_WITH_TIMEOUT(
            nearly_equal(
                pill->property("width").toReal(),
                root->property("pid_pill_target_width").toReal()),
            1000);
        QCOMPARE(root->property("width").toReal(), 80.0);
        QCOMPARE(pill->property("height").toReal(), 80.0);
        QVERIFY(pill->property("width").toReal() >= 80.0);

        QVERIFY(QMetaObject::invokeMethod(root.get(), "request_pid_retract"));
        QTRY_VERIFY_WITH_TIMEOUT(
            root->property("pid_phase").toString().isEmpty(),
            5000);
        QVERIFY(!pill->property("visible").toBool());

        QVERIFY(root->setProperty("pid_reveal_enabled", false));
        QVERIFY(QMetaObject::invokeMethod(root.get(), "request_pid_reveal"));
        QCOMPARE(root->property("pid_phase").toString(), QString());
    }

    void animated_mark_live_size_changes_retarget_active_pid_animations()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

VNM_AnimatedMark {
    objectName: "animated_mark"
    mark_size: 20
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/animated_mark_pid_retarget_contract.qml");
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        QObject* pill = find_descendant(
            root.get(), QStringLiteral("vnm_mark_pid_pill"));
        QVERIFY(pill != nullptr);
        QVERIFY(root->setProperty("hover_active", true));
        QVERIFY(QMetaObject::invokeMethod(root.get(), "request_pid_reveal"));
        QTRY_COMPARE_WITH_TIMEOUT(
            root->property("pid_phase").toString(),
            QStringLiteral("elongating"),
            3000);

        const qreal initial_target =
            root->property("pid_pill_target_width").toReal();
        QVERIFY(root->setProperty("mark_size", 90.0));
        const qreal enlarged_target =
            root->property("pid_pill_target_width").toReal();
        QVERIFY(enlarged_target > initial_target);
        QVERIFY(enlarged_target >= 90.0);
        QTRY_COMPARE_WITH_TIMEOUT(
            root->property("pid_phase").toString(),
            QStringLiteral("revealed"),
            5000);
        QVERIFY(nearly_equal(
            pill->property("width").toReal(), enlarged_target));

        QVERIFY(root->setProperty("mark_size", 20.0));
        QTRY_VERIFY_WITH_TIMEOUT(
            nearly_equal(
                pill->property("width").toReal(),
                root->property("pid_pill_target_width").toReal()),
            1000);
        QVERIFY(QMetaObject::invokeMethod(root.get(), "request_pid_retract"));
        QCOMPARE(
            root->property("pid_phase").toString(),
            QStringLiteral("retracting"));

        QVERIFY(root->setProperty("mark_size", 36.0));
        QTRY_VERIFY_WITH_TIMEOUT(
            root->property("pid_phase").toString().isEmpty(),
            5000);
        QVERIFY(nearly_equal(pill->property("width").toReal(), 36.0));
        QCOMPARE(root->property("width").toReal(), 36.0);
        QCOMPARE(root->property("height").toReal(), 36.0);
        QVERIFY(!pill->property("visible").toBool());
    }

    void animated_mark_pid_escape_disablement_and_focus_are_lifecycle_safe()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import QtQuick.Window
import VNM_Chrome

Window {
    width: 240
    height: 80
    visible: true

    Item {
        objectName: "focus_before"
        focus: true
    }

    Item {
        objectName: "focus_after"
    }

    VNM_AnimatedMark {
        objectName: "animated_mark"
        anchors.centerIn: parent
        mark_size: 20
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/animated_mark_pid_lifecycle_contract.qml");
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        auto* window = qobject_cast<QQuickWindow*>(root.get());
        auto* mark = find_item(root.get(), QStringLiteral("animated_mark"));
        auto* pid_edit = find_item(root.get(), QStringLiteral("vnm_mark_pid_edit"));
        auto* focus_before = find_item(root.get(), QStringLiteral("focus_before"));
        auto* focus_after = find_item(root.get(), QStringLiteral("focus_after"));
        QVERIFY(window       != nullptr);
        QVERIFY(mark         != nullptr);
        QVERIFY(pid_edit     != nullptr);
        QVERIFY(focus_before != nullptr);
        QVERIFY(focus_after  != nullptr);

        QVERIFY(QMetaObject::invokeMethod(focus_before, "forceActiveFocus"));
        QTRY_VERIFY_WITH_TIMEOUT(focus_before->hasActiveFocus(), 1000);
        QVERIFY(mark->setProperty("hover_active", true));

        QVERIFY(QMetaObject::invokeMethod(mark, "request_pid_reveal"));
        QCOMPARE(mark->property("pid_phase").toString(), QStringLiteral("forming"));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(
            mark->property("pid_phase").toString().isEmpty(),
            1000);
        QVERIFY(focus_before->hasActiveFocus());

        QVERIFY(QMetaObject::invokeMethod(mark, "request_pid_reveal"));
        QTRY_COMPARE_WITH_TIMEOUT(
            mark->property("pid_phase").toString(),
            QStringLiteral("elongating"),
            3000);
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(
            mark->property("pid_phase").toString().isEmpty(),
            3000);
        QVERIFY(focus_before->hasActiveFocus());

        QVERIFY(QMetaObject::invokeMethod(mark, "request_pid_reveal"));
        QTRY_COMPARE_WITH_TIMEOUT(
            mark->property("pid_phase").toString(),
            QStringLiteral("revealed"),
            5000);
        QVERIFY(pid_edit->hasActiveFocus());
        const QString clipboard_sentinel = QStringLiteral("unchanged clipboard");
        QGuiApplication::clipboard()->setText(clipboard_sentinel);
        QCOMPARE(QGuiApplication::clipboard()->text(), clipboard_sentinel);
        QVERIFY(pid_edit->property("selectedText").toString().isEmpty());
        QTest::keyClick(window, Qt::Key_C, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(
            QGuiApplication::clipboard()->text(),
            QString::number(QCoreApplication::applicationPid()),
            1000);
        QTRY_VERIFY_WITH_TIMEOUT(
            mark->property("pid_phase").toString().isEmpty(),
            5000);
        QTRY_VERIFY_WITH_TIMEOUT(focus_before->hasActiveFocus(), 1000);

        QVERIFY(QMetaObject::invokeMethod(mark, "request_pid_reveal"));
        QTRY_COMPARE_WITH_TIMEOUT(
            mark->property("pid_phase").toString(),
            QStringLiteral("revealed"),
            5000);
        QVERIFY(pid_edit->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(
            mark->property("pid_phase").toString().isEmpty(),
            5000);
        QTRY_VERIFY_WITH_TIMEOUT(focus_before->hasActiveFocus(), 1000);

        QVERIFY(QMetaObject::invokeMethod(mark, "request_pid_reveal"));
        QTRY_COMPARE_WITH_TIMEOUT(
            mark->property("pid_phase").toString(),
            QStringLiteral("revealed"),
            5000);
        QVERIFY(pid_edit->hasActiveFocus());
        QVERIFY(QMetaObject::invokeMethod(focus_after, "forceActiveFocus"));
        QTRY_VERIFY_WITH_TIMEOUT(focus_after->hasActiveFocus(), 1000);
        QVERIFY(mark->setProperty("pid_reveal_enabled", false));
        QTRY_VERIFY_WITH_TIMEOUT(
            mark->property("pid_phase").toString().isEmpty(),
            5000);
        QVERIFY(focus_after->hasActiveFocus());

        QVERIFY(mark->setProperty("pid_reveal_enabled", true));
        QVERIFY(QMetaObject::invokeMethod(mark, "request_pid_reveal"));
        QCOMPARE(mark->property("pid_phase").toString(), QStringLiteral("forming"));
        QVERIFY(mark->setProperty("pid_reveal_enabled", false));
        QCOMPARE(mark->property("pid_phase").toString(), QString());
        QVERIFY(focus_after->hasActiveFocus());
    }

    void system_window_tracks_alt_modifier_state()
    {
        vnm_qml_chrome::System_window system_window;
        QSignalSpy alt_spy(
            &system_window,
            &vnm_qml_chrome::System_window::alt_modifier_active_changed);
        QObject event_target;

        // Windows reports the Alt key's own press with the AltModifier bit
        // removed from the event modifiers, and the release with the bit
        // still set; the event type must carry the state.
        QKeyEvent alt_press(QEvent::KeyPress, Qt::Key_Alt, Qt::NoModifier);
        QCoreApplication::sendEvent(&event_target, &alt_press);
        QVERIFY(system_window.alt_modifier_active());
        QCOMPARE(alt_spy.count(), 1);

        QKeyEvent alt_repeat(QEvent::KeyPress, Qt::Key_Alt, Qt::NoModifier);
        QCoreApplication::sendEvent(&event_target, &alt_repeat);
        QCOMPARE(alt_spy.count(), 1);

        QKeyEvent alt_release(QEvent::KeyRelease, Qt::Key_Alt, Qt::AltModifier);
        QCoreApplication::sendEvent(&event_target, &alt_release);
        QVERIFY(!system_window.alt_modifier_active());
        QCOMPARE(alt_spy.count(), 2);
    }

    void resize_corner_ownership_prefers_titlebar_and_bottom_layers()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    width: 240
    height: 140

    VNM_ChromeSideResizeLayer {
        objectName: "side_layer"
        anchors.fill: parent
        resize_target_extent: 8
    }

    VNM_ChromeBottomResizeLayer {
        objectName: "bottom_layer"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: implicitHeight
        resize_target_extent: 8
    }

    VNM_ChromeTitleBar {
        objectName: "chrome_titlebar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 32
        resize_target_extent: 8
    }
}
)";

        std::unique_ptr<QObject> root_object = create_qml_object(
            engine, qml_source, "qrc:/tests/resize_corner_ownership_contract.qml");
        QVERIFY(root_object != nullptr);
        auto* root = qobject_cast<QQuickItem*>(root_object.get());
        QVERIFY(root != nullptr);
        root->ensurePolished();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        auto* top_owner = root->childAt(2, 2);
        QVERIFY(top_owner != nullptr);
        QCOMPARE(top_owner->objectName(), QStringLiteral("chrome_titlebar"));

        auto* titlebar = qobject_cast<QQuickItem*>(
            find_descendant(root_object.get(), QStringLiteral("chrome_titlebar")));
        QVERIFY(titlebar != nullptr);
        auto* top_corner_owner = titlebar->childAt(1, 1);
        QVERIFY(top_corner_owner != nullptr);
        QCOMPARE(top_corner_owner->objectName(), QStringLiteral("top_left_resize_area"));

        auto* bottom_owner = root->childAt(1, 138);
        QVERIFY(bottom_owner != nullptr);
        QCOMPARE(bottom_owner->objectName(), QStringLiteral("bottom_layer"));

        auto* bottom_layer = qobject_cast<QQuickItem*>(
            find_descendant(root_object.get(), QStringLiteral("bottom_layer")));
        QVERIFY(bottom_layer != nullptr);
        auto* bottom_corner_owner = bottom_layer->childAt(1, 6);
        QVERIFY(bottom_corner_owner != nullptr);
        QCOMPARE(bottom_corner_owner->objectName(), QStringLiteral("bottom_left_resize_area"));
    }

    void side_resize_layer_adds_vertical_edges_inside_corner_bands()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    width: 240
    height: 80

    VNM_ChromeSideResizeLayer {
        id: side_layer
        objectName: "side_layer"
        anchors.fill: parent
        resize_target_extent: 8
    }

    function top_left_edges() {
        return side_layer.resize_edges_for_y(Qt.LeftEdge, 2)
    }

    function middle_left_edges() {
        return side_layer.resize_edges_for_y(Qt.LeftEdge, 40)
    }

    function top_right_edges() {
        return side_layer.resize_edges_for_y(Qt.RightEdge, 2)
    }

    function bottom_right_edges() {
        return side_layer.resize_edges_for_y(Qt.RightEdge, 78)
    }

    function top_left_cursor() {
        return side_layer.resize_cursor_for_y(Qt.LeftEdge, 2)
    }

    function middle_left_cursor() {
        return side_layer.resize_cursor_for_y(Qt.LeftEdge, 40)
    }

    function bottom_left_cursor() {
        return side_layer.resize_cursor_for_y(Qt.LeftEdge, 78)
    }

    function top_right_cursor() {
        return side_layer.resize_cursor_for_y(Qt.RightEdge, 2)
    }

    function bottom_right_cursor() {
        return side_layer.resize_cursor_for_y(Qt.RightEdge, 78)
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/side_resize_corner_band_contract.qml");
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        QObject* left_resize_area =
            find_descendant(root.get(), QStringLiteral("left_resize_area"));
        QVERIFY(left_resize_area != nullptr);

        QVariant value;
        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "top_left_edges",
            Q_RETURN_ARG(QVariant, value)));
        QCOMPARE(value.toInt(), int(Qt::LeftEdge | Qt::TopEdge));

        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "middle_left_edges",
            Q_RETURN_ARG(QVariant, value)));
        QCOMPARE(value.toInt(), int(Qt::LeftEdge));

        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "top_right_edges",
            Q_RETURN_ARG(QVariant, value)));
        QCOMPARE(value.toInt(), int(Qt::RightEdge | Qt::TopEdge));

        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "bottom_right_edges",
            Q_RETURN_ARG(QVariant, value)));
        QCOMPARE(value.toInt(), int(Qt::RightEdge | Qt::BottomEdge));

        const QVariantMap top_corner_mouse{{QStringLiteral("y"), 2.0}};
        QVERIFY(QMetaObject::invokeMethod(
            left_resize_area,
            "resolved_edges",
            Q_RETURN_ARG(QVariant, value),
            Q_ARG(QVariant, QVariant(top_corner_mouse))));
        QCOMPARE(value.toInt(), int(Qt::LeftEdge | Qt::TopEdge));

        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "top_left_cursor",
            Q_RETURN_ARG(QVariant, value)));
        QCOMPARE(value.toInt(), int(Qt::SizeFDiagCursor));

        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "middle_left_cursor",
            Q_RETURN_ARG(QVariant, value)));
        QCOMPARE(value.toInt(), int(Qt::SizeHorCursor));

        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "bottom_left_cursor",
            Q_RETURN_ARG(QVariant, value)));
        QCOMPARE(value.toInt(), int(Qt::SizeBDiagCursor));

        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "top_right_cursor",
            Q_RETURN_ARG(QVariant, value)));
        QCOMPARE(value.toInt(), int(Qt::SizeBDiagCursor));

        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "bottom_right_cursor",
            Q_RETURN_ARG(QVariant, value)));
        QCOMPARE(value.toInt(), int(Qt::SizeFDiagCursor));
    }

    void resize_layers_forward_inner_resize_requests()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    width: 240
    height: 80

    VNM_ChromeSideResizeLayer {
        objectName: "side_layer"
        anchors.fill: parent
        resize_target_extent: 8
    }

    VNM_ChromeBottomResizeLayer {
        objectName: "bottom_layer"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: implicitHeight
        resize_target_extent: 8
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/resize_forwarding_contract.qml");
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        QObject* side_layer = find_descendant(root.get(), QStringLiteral("side_layer"));
        QVERIFY(side_layer != nullptr);
        QObject* left_resize_area =
            find_descendant(root.get(), QStringLiteral("left_resize_area"));
        QVERIFY(left_resize_area != nullptr);
        QSignalSpy side_spy(side_layer, SIGNAL(resize_requested(int)));
        QVERIFY(side_spy.isValid());

        QVERIFY(QMetaObject::invokeMethod(
            left_resize_area, "resize_requested", Q_ARG(int, int(Qt::LeftEdge))));
        QCOMPARE(side_spy.count(), 1);
        QCOMPARE(side_spy.takeFirst().at(0).toInt(), int(Qt::LeftEdge));

        QObject* bottom_layer = find_descendant(root.get(), QStringLiteral("bottom_layer"));
        QVERIFY(bottom_layer != nullptr);
        QObject* bottom_left_resize_area =
            find_descendant(root.get(), QStringLiteral("bottom_left_resize_area"));
        QVERIFY(bottom_left_resize_area != nullptr);
        QSignalSpy bottom_spy(bottom_layer, SIGNAL(resize_requested(int)));
        QVERIFY(bottom_spy.isValid());

        QVERIFY(QMetaObject::invokeMethod(
            bottom_left_resize_area,
            "resize_requested",
            Q_ARG(int, int(Qt::LeftEdge | Qt::BottomEdge))));
        QCOMPARE(bottom_spy.count(), 1);
        QCOMPARE(bottom_spy.takeFirst().at(0).toInt(), int(Qt::LeftEdge | Qt::BottomEdge));
    }

    void titlebar_move_threshold_and_signal_are_synchronous()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import VNM_Chrome

Item {
    id: root

    width: 420
    height: 48
    property int move_count: 0
    property bool move_seen_before_function_return: false

    VNM_ChromeTitleBar {
        id: chrome_titlebar
        objectName: "chrome_titlebar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        title: "Move"

        onMove_requested: {
            root.move_count += 1
            root.move_seen_before_function_return = true
        }
    }

    QtObject {
        id: move_probe

        property real system_move_press_x: 0
        property real system_move_press_y: 0
        property bool system_move_started: false
    }

    function reset_probe() {
        root.move_count = 0
        root.move_seen_before_function_return = false
        move_probe.system_move_press_x = 0
        move_probe.system_move_press_y = 0
        move_probe.system_move_started = false
    }

    function under_threshold_move_is_ignored() {
        reset_probe()
        const mouse = {
            x: chrome_titlebar.move_drag_threshold - 0.25,
            y: 0,
            buttons: Qt.LeftButton,
            accepted: false,
        }
        chrome_titlebar.maybe_start_system_move(move_probe, mouse)
        return root.move_count === 0
            && !move_probe.system_move_started
            && mouse.accepted === false
    }

    function at_threshold_move_is_synchronous() {
        reset_probe()
        const mouse = {
            x: chrome_titlebar.move_drag_threshold,
            y: 0,
            buttons: Qt.LeftButton,
            accepted: false,
        }
        chrome_titlebar.maybe_start_system_move(move_probe, mouse)
        return root.move_count === 1
            && root.move_seen_before_function_return
            && move_probe.system_move_started
            && mouse.accepted === true
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/titlebar_move_contract.qml");
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        QVariant under_threshold_result;
        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "under_threshold_move_is_ignored",
            Q_RETURN_ARG(QVariant, under_threshold_result)));
        QVERIFY(under_threshold_result.toBool());

        QVariant at_threshold_result;
        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "at_threshold_move_is_synchronous",
            Q_RETURN_ARG(QVariant, at_threshold_result)));
        QVERIFY(at_threshold_result.toBool());
    }

    void titlebar_title_editing_is_opt_in_and_commits_completed_edits()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import QtQuick.Window
import VNM_Chrome

Window {
    id: root

    width: 420
    height: 48
    visible: true

    VNM_ChromeTitleBar {
        id: chrome_titlebar
        objectName: "chrome_titlebar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        title: "Process title"
    }

    Item {
        id: focus_sink
        objectName: "focus_sink"
    }

    QtObject {
        id: mouse_probe

        property int button: Qt.LeftButton
        property int modifiers: Qt.AltModifier
        property bool accepted: false
    }

    function try_alt_click() {
        mouse_probe.accepted = false
        return chrome_titlebar.maybe_begin_title_edit(mouse_probe)
    }
}
)";

        std::unique_ptr<QObject> root = create_qml_object(
            engine, qml_source, "qrc:/tests/titlebar_title_editing_contract.qml");
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        QObject* titlebar = find_descendant(root.get(), QStringLiteral("chrome_titlebar"));
        QObject* editor   = find_descendant(root.get(), QStringLiteral("title_editor"));
        QObject* editor_frame = find_descendant(
            root.get(), QStringLiteral("title_editor_frame"));
        QObject* animated_mark = find_descendant(
            root.get(), QStringLiteral("vnm_animated_mark"));
        QObject* focus_sink = find_descendant(root.get(), QStringLiteral("focus_sink"));
        QVERIFY(titlebar != nullptr);
        QVERIFY(editor   != nullptr);
        QVERIFY(editor_frame != nullptr);
        QVERIFY(animated_mark != nullptr);
        QVERIFY(focus_sink != nullptr);
        QCOMPARE(titlebar->property("title_editing_enabled").toBool(), false);

        QVariant began_editing;
        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "try_alt_click",
            Q_RETURN_ARG(QVariant, began_editing)));
        QCOMPARE(began_editing.toBool(), false);
        QCOMPARE(editor->property("visible").toBool(), false);

        QVERIFY(titlebar->setProperty("title_editing_enabled", true));
        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "try_alt_click",
            Q_RETURN_ARG(QVariant, began_editing)));
        QCOMPARE(began_editing.toBool(), true);
        QCOMPARE(editor->property("visible").toBool(), true);
        QCOMPARE(editor->property("text").toString(), QStringLiteral("Process title"));
        QCOMPARE(editor_frame->property("visible").toBool(), true);
        QCOMPARE(animated_mark->property("alt_reveal_forced").toBool(), true);
        QCOMPARE(animated_mark->property("alt_click_enabled").toBool(), true);

        QVERIFY(editor->setProperty("text", QStringLiteral("Draft title")));
        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "try_alt_click",
            Q_RETURN_ARG(QVariant, began_editing)));
        QCOMPARE(began_editing.toBool(), true);
        QCOMPARE(editor->property("text").toString(), QStringLiteral("Draft title"));

        QSignalSpy accepted_spy(titlebar, SIGNAL(title_edit_accepted(QString)));
        QVERIFY(accepted_spy.isValid());
        QVERIFY(editor->setProperty("text", QStringLiteral("User title")));
        QVERIFY(QMetaObject::invokeMethod(editor, "accepted"));
        QCOMPARE(accepted_spy.count(), 1);
        QCOMPARE(
            accepted_spy.takeFirst().at(0).toString(),
            QStringLiteral("User title"));
        QCOMPARE(editor->property("visible").toBool(), false);
        QCOMPARE(editor_frame->property("visible").toBool(), false);
        QCOMPARE(animated_mark->property("alt_reveal_forced").toBool(), false);
        QCOMPARE(titlebar->property("title").toString(), QStringLiteral("Process title"));

        const QVariantMap alt_mouse{
            {QStringLiteral("button"),    int(Qt::LeftButton)},
            {QStringLiteral("modifiers"), int(Qt::AltModifier)},
            {QStringLiteral("accepted"),  false},
        };
        QVERIFY(QMetaObject::invokeMethod(focus_sink, "forceActiveFocus"));
        QTRY_VERIFY_WITH_TIMEOUT(focus_sink->property("activeFocus").toBool(), 1000);
        QVERIFY(animated_mark->setProperty("hover_active", true));
        QVERIFY(QMetaObject::invokeMethod(animated_mark, "request_pid_reveal"));
        QCOMPARE(
            animated_mark->property("pid_phase").toString(),
            QStringLiteral("forming"));
        QVERIFY(QMetaObject::invokeMethod(
            animated_mark,
            "handle_primary_press",
            Q_ARG(QVariant, QVariant(alt_mouse))));
        QCOMPARE(animated_mark->property("pid_phase").toString(), QString());
        QCOMPARE(editor_frame->property("visible").toBool(), true);
        QTRY_VERIFY_WITH_TIMEOUT(editor->property("activeFocus").toBool(), 1000);
        QTest::qWait(500);
        QCOMPARE(animated_mark->property("pid_phase").toString(), QString());
        QCOMPARE(editor->property("activeFocus").toBool(), true);

        auto* window = qobject_cast<QWindow*>(root.get());
        QVERIFY(window != nullptr);
        QTest::keyClick(window, Qt::Key_Escape);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QCOMPARE(accepted_spy.count(), 0);
        QCOMPARE(editor_frame->property("visible").toBool(), false);
        QTRY_VERIFY_WITH_TIMEOUT(
            !animated_mark->property("alt_reveal_active").toBool(),
            3000);

        QVERIFY(animated_mark->setProperty("hover_active", true));
        QVERIFY(QMetaObject::invokeMethod(animated_mark, "request_pid_reveal"));
        QTRY_COMPARE_WITH_TIMEOUT(
            animated_mark->property("pid_phase").toString(),
            QStringLiteral("revealed"),
            5000);
        QVERIFY(QMetaObject::invokeMethod(
            animated_mark,
            "handle_primary_press",
            Q_ARG(QVariant, QVariant(alt_mouse))));
        QCOMPARE(editor_frame->property("visible").toBool(), true);
        QCOMPARE(animated_mark->property("alt_reveal_forced").toBool(), true);
        QCOMPARE(
            animated_mark->property("pid_phase").toString(),
            QStringLiteral("retracting"));
        QTest::keyClick(window, Qt::Key_Escape);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QCOMPARE(accepted_spy.count(), 0);
        QCOMPARE(editor_frame->property("visible").toBool(), false);
        QCOMPARE(titlebar->property("title").toString(), QStringLiteral("Process title"));
        QTRY_VERIFY_WITH_TIMEOUT(
            animated_mark->property("pid_phase").toString().isEmpty(),
            5000);

        QVERIFY(QMetaObject::invokeMethod(
            root.get(),
            "try_alt_click",
            Q_RETURN_ARG(QVariant, began_editing)));
        QCOMPARE(began_editing.toBool(), true);
        QVERIFY(editor->setProperty("text", QStringLiteral("Focus loss title")));
        QVERIFY(QMetaObject::invokeMethod(focus_sink, "forceActiveFocus"));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QCOMPARE(accepted_spy.count(), 1);
        QCOMPARE(
            accepted_spy.takeFirst().at(0).toString(),
            QStringLiteral("Focus loss title"));
        QCOMPARE(editor_frame->property("visible").toBool(), false);
        QCOMPARE(animated_mark->property("alt_reveal_forced").toBool(), false);
        QCOMPARE(focus_sink->property("activeFocus").toBool(), true);
        QCOMPARE(titlebar->property("title").toString(), QStringLiteral("Process title"));
    }
};

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");

    QGuiApplication app(argc, argv);
    Vnm_chrome_contract_tests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "vnm_chrome_contract_tests.moc"
