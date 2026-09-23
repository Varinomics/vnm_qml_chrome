#include "vnm_device_pixel_geometry_item.h"

#include "vnm_qml_chrome/vnm_chrome_geometry.h"

#include <QCoreApplication>
#include <QEvent>
#include <QQuickWindow>
#include <QTransform>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace
{

constexpr qreal k_transform_epsilon = 1.0e-6;

bool is_finite(qreal value)
{
    return std::isfinite(value);
}

bool is_finite(const QPointF& point)
{
    return is_finite(point.x()) && is_finite(point.y());
}

bool close(qreal left, qreal right, qreal scale = 1.0)
{
    return std::abs(left - right) <=
        k_transform_epsilon * std::max(qreal{1.0}, std::abs(scale));
}

qreal finite_nonnegative(qreal value)
{
    return is_finite(value) && value > 0.0 ? value : 0.0;
}

struct corrected_axis_t
{
    qreal position = 0.0;
    qreal extent = 0.0;
};

corrected_axis_t corrected_axis(
    qreal scene_origin,
    qreal logical_extent,
    qreal scale,
    qreal dpr)
{
    const qreal first_edge = vnm_qml_chrome::snapped_logical_edge(scene_origin, dpr);
    // Round the span independently so translation cannot resize a painted path
    // and force its renderer to rebuild curve geometry.
    const qreal physical_scale = scale * dpr;
    const qreal physical_extent = std::round(logical_extent * physical_scale);

    corrected_axis_t corrected;
    corrected.position = (first_edge - scene_origin) / scale;
    corrected.extent = physical_extent / physical_scale;
    return corrected;
}

} // namespace

VNM_Device_pixel_geometry_item::VNM_Device_pixel_geometry_item(QQuickItem* parent)

:
    QQuickItem(parent)
{
    setAcceptedMouseButtons(Qt::NoButton);
    // Observe scene-transform changes too, including QML Translate/Scale items.
    setFlag(ItemObservesViewport, true);

    m_window_connection = connect(this, &QQuickItem::windowChanged,
            this, &VNM_Device_pixel_geometry_item::reconnect_window);
    refresh_scene_observers();
    reconnect_window(window());
    apply_corrected_geometry();
}

VNM_Device_pixel_geometry_item::~VNM_Device_pixel_geometry_item()
{
    // QQuickItem can emit windowChanged after this derived destructor returns.
    QObject::disconnect(m_window_connection);
    clear_scene_observers();
    QObject::disconnect(m_window_dpr_connection);
    QObject::disconnect(m_window_invalidated_connection);
    QObject::disconnect(m_window_initialized_connection);
    m_scene_graph_lost = false;
    if (m_observed_window) {
        m_observed_window->removeEventFilter(this);
    }
    setFlag(ItemObservesViewport, false);
}

qreal VNM_Device_pixel_geometry_item::device_pixel_ratio() const
{
    return m_device_pixel_ratio;
}

bool VNM_Device_pixel_geometry_item::snapping_active() const
{
    return m_snapping_active;
}

VNM_Device_pixel_geometry_item::scene_mapping_t
VNM_Device_pixel_geometry_item::scene_mapping() const
{
    scene_mapping_t mapping;
    const QQuickItem* host = parentItem();
    if (!host) {
        return mapping;
    }

    // Subtracting mapped points lets translation cancellation perturb the
    // inferred scale, including which side of a half-pixel span is rounded.
    // The affine linear coefficients have no translation term to cancel.
    const QTransform transform = host->itemTransform(nullptr, nullptr);
    const QPointF origin = transform.map(QPointF(0.0, 0.0));
    if (!transform.isAffine() || !is_finite(origin)) {
        return mapping;
    }

    const qreal x_scale = transform.m11();
    const qreal y_scale = transform.m22();
    if (!is_finite(x_scale) || !is_finite(y_scale) ||
        x_scale <= k_transform_epsilon || y_scale <= k_transform_epsilon ||
        !close(transform.m12(), 0.0, x_scale) ||
        !close(transform.m21(), 0.0, y_scale) ||
        !close(x_scale, y_scale, std::max(x_scale, y_scale)))
    {
        return mapping;
    }

    mapping.origin = origin;
    mapping.scale = (x_scale + y_scale) / 2.0;
    mapping.supported = true;
    return mapping;
}

qreal VNM_Device_pixel_geometry_item::current_device_pixel_ratio() const
{
    return vnm_qml_chrome::normalized_device_pixel_ratio(
        window() ? window()->effectiveDevicePixelRatio() : 1.0);
}

void VNM_Device_pixel_geometry_item::apply_corrected_geometry()
{
    if (m_applying_geometry) {
        return;
    }

    m_applying_geometry = true;
    const scene_mapping_t mapping = scene_mapping();
    const qreal dpr = current_device_pixel_ratio();
    QQuickItem* host = parentItem();
    if (!host || !mapping.supported) {
        apply_logical_fallback();
        update_reported_state(dpr, false);
        m_applying_geometry = false;
        return;
    }

    const qreal logical_width = finite_nonnegative(host->width());
    const qreal logical_height = finite_nonnegative(host->height());
    const corrected_axis_t horizontal = corrected_axis(
        mapping.origin.x(),
        logical_width,
        mapping.scale,
        dpr);
    const corrected_axis_t vertical = corrected_axis(
        mapping.origin.y(),
        logical_height,
        mapping.scale,
        dpr);

    if (!is_finite(horizontal.position) || !is_finite(horizontal.extent) ||
        !is_finite(vertical.position) || !is_finite(vertical.extent))
    {
        apply_logical_fallback();
        update_reported_state(dpr, false);
        m_applying_geometry = false;
        return;
    }

    setX(horizontal.position);
    setY(vertical.position);
    setWidth(horizontal.extent);
    setHeight(vertical.extent);
    update_reported_state(dpr, true);
    m_applying_geometry = false;
}

void VNM_Device_pixel_geometry_item::apply_logical_fallback()
{
    const QQuickItem* host = parentItem();
    setX(0.0);
    setY(0.0);
    setWidth(host ? finite_nonnegative(host->width()) : 0.0);
    setHeight(host ? finite_nonnegative(host->height()) : 0.0);
}

void VNM_Device_pixel_geometry_item::schedule_corrected_geometry()
{
    if (m_update_queued) {
        return;
    }

    m_update_queued = true;
    if (!QMetaObject::invokeMethod(
            this,
            [this]() {
                m_update_queued = false;
                apply_corrected_geometry();
            },
            Qt::QueuedConnection))
    {
        m_update_queued = false;
    }
}

void VNM_Device_pixel_geometry_item::refresh_scene_observers()
{
    clear_scene_observers();
    setFlag(ItemObservesViewport, true);

    const auto update = [this]() {
        apply_corrected_geometry();
        // An ancestor notification can precede Qt's scene-transform update.
        schedule_corrected_geometry();
    };
    const auto refresh = [this]() {
        refresh_scene_observers();
        apply_corrected_geometry();
        schedule_corrected_geometry();
    };

    for (QQuickItem* item = parentItem(); item; item = item->parentItem()) {
        m_scene_connections.push_back(connect(
            item, &QQuickItem::xChanged, this, update));
        m_scene_connections.push_back(connect(
            item, &QQuickItem::yChanged, this, update));
        m_scene_connections.push_back(connect(
            item, &QQuickItem::widthChanged, this, update));
        m_scene_connections.push_back(connect(
            item, &QQuickItem::heightChanged, this, update));
        m_scene_connections.push_back(connect(
            item, &QQuickItem::scaleChanged, this, update));
        m_scene_connections.push_back(connect(
            item, &QQuickItem::rotationChanged, this, update));
        m_scene_connections.push_back(connect(
            item, &QQuickItem::parentChanged, this, refresh));
    }
}

void VNM_Device_pixel_geometry_item::clear_scene_observers()
{
    for (const QMetaObject::Connection& connection : m_scene_connections) {
        QObject::disconnect(connection);
    }
    m_scene_connections.clear();
}

void VNM_Device_pixel_geometry_item::reconnect_window(QQuickWindow* window)
{
    QObject::disconnect(m_window_dpr_connection);
    QObject::disconnect(m_window_invalidated_connection);
    QObject::disconnect(m_window_initialized_connection);
    m_scene_graph_lost = false;
    if (m_observed_window) {
        m_observed_window->removeEventFilter(this);
    }
    m_observed_window = window;
    if (window) {
        window->installEventFilter(this);
        // Window lifecycle signals originate on the render thread. Shape paths
        // belong to the GUI thread and must be rebuilt there after resource loss.
        m_window_invalidated_connection = connect(
            window, &QQuickWindow::sceneGraphInvalidated, this,
            [this, observed = QPointer<QQuickWindow>(window)]() {
                if (observed && observed == m_observed_window) {
                    m_scene_graph_lost = true;
                }
            }, Qt::QueuedConnection);
        m_window_initialized_connection = connect(
            window, &QQuickWindow::sceneGraphInitialized, this,
            [this, observed = QPointer<QQuickWindow>(window)]() {
                if (observed && observed == m_observed_window && m_scene_graph_lost) {
                    m_scene_graph_lost = false;
                    emit scene_graph_recreated();
                }
            }, Qt::QueuedConnection);
        m_window_dpr_connection = connect(
            window,
            &QQuickWindow::devicePixelRatioChanged,
            this,
            [this]() {
                apply_corrected_geometry();
                schedule_corrected_geometry();
            });
    }
    else {
        m_window_dpr_connection = {};
    }

    apply_corrected_geometry();
    schedule_corrected_geometry();
}

bool VNM_Device_pixel_geometry_item::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_observed_window &&
        event->type() == QEvent::DevicePixelRatioChange)
    {
        apply_corrected_geometry();
        schedule_corrected_geometry();
    }
    return QQuickItem::eventFilter(watched, event);
}

void VNM_Device_pixel_geometry_item::update_reported_state(
    qreal dpr,
    bool active)
{
    if (!qFuzzyCompare(m_device_pixel_ratio, dpr)) {
        m_device_pixel_ratio = dpr;
        emit device_pixel_ratio_changed();
    }
    if (m_snapping_active != active) {
        m_snapping_active = active;
        emit snapping_active_changed();
    }
}

void VNM_Device_pixel_geometry_item::geometryChange(
    const QRectF& new_geometry,
    const QRectF& old_geometry)
{
    QQuickItem::geometryChange(new_geometry, old_geometry);
    if (!m_applying_geometry && new_geometry != old_geometry) {
        apply_corrected_geometry();
        schedule_corrected_geometry();
    }
}

void VNM_Device_pixel_geometry_item::itemChange(
    ItemChange change,
    const ItemChangeData& value)
{
    QQuickItem::itemChange(change, value);

    if (change == ItemParentHasChanged || change == ItemSceneChange) {
        refresh_scene_observers();
        reconnect_window(window());
    }
    else if (change == ItemRotationHasChanged ||
        change == ItemTransformHasChanged ||
        change == ItemDevicePixelRatioHasChanged)
    {
        apply_corrected_geometry();
        schedule_corrected_geometry();
    }
}
