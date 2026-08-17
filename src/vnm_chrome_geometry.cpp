#include "vnm_qml_chrome/vnm_chrome_geometry.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr qreal k_snap_epsilon = 0.000001;

} // namespace

namespace vnm_qml_chrome {

qreal normalized_device_pixel_ratio(qreal device_pixel_ratio)
{
    return
        std::isfinite(device_pixel_ratio) && device_pixel_ratio > 0.0
            ? device_pixel_ratio
            : 1.0;
}

qreal snapped_logical_edge(
    qreal logical_edge,
    qreal device_pixel_ratio)
{
    const qreal dpr = normalized_device_pixel_ratio(device_pixel_ratio);
    return std::round(logical_edge * dpr) / dpr;
}

QRectF snapped_logical_rect(
    const QRectF& logical_rect,
    qreal         device_pixel_ratio)
{
    const qreal left   = snapped_logical_edge(logical_rect.left(),   device_pixel_ratio);
    const qreal top    = snapped_logical_edge(logical_rect.top(),    device_pixel_ratio);
    const qreal right  = snapped_logical_edge(logical_rect.right(),  device_pixel_ratio);
    const qreal bottom = snapped_logical_edge(logical_rect.bottom(), device_pixel_ratio);

    return QRectF(
        left,
        top,
        std::max<qreal>(0.0, right - left),
        std::max<qreal>(0.0, bottom - top));
}

bool rect_has_snapped_physical_edges(
    const QRectF& logical_rect,
    qreal         device_pixel_ratio)
{
    const qreal dpr = normalized_device_pixel_ratio(device_pixel_ratio);
    const auto edge_is_snapped = [dpr](qreal logical_edge) {
        const qreal physical_edge = logical_edge * dpr;
        return std::abs(physical_edge - std::round(physical_edge)) <= k_snap_epsilon;
    };

    return
        edge_is_snapped(logical_rect.left())   &&
        edge_is_snapped(logical_rect.top())    &&
        edge_is_snapped(logical_rect.right())  &&
        edge_is_snapped(logical_rect.bottom());
}

bool physical_extent_matches_logical(
    qreal logical_extent,
    qreal physical_extent,
    qreal device_pixel_ratio)
{
    if (!std::isfinite(logical_extent) || !std::isfinite(physical_extent)) {
        return false;
    }
    if (physical_extent <= 0.0) {
        return false;
    }

    const qreal dpr = normalized_device_pixel_ratio(device_pixel_ratio);
    const qreal implied_logical_extent = physical_extent / dpr;

    return
        std::abs(implied_logical_extent - logical_extent) <=
        k_physical_extent_agreement_tolerance;
}

qreal physical_far_edge(
    qreal logical_edge,
    qreal physical_extent,
    qreal device_pixel_ratio)
{
    if (!physical_extent_matches_logical(
            logical_edge,
            physical_extent,
            device_pixel_ratio)) {
        return snapped_logical_edge(logical_edge, device_pixel_ratio);
    }

    const qreal dpr = normalized_device_pixel_ratio(device_pixel_ratio);

    return std::max<qreal>(0.0, physical_extent / dpr);
}

qreal resize_inward_extent(
    qreal frame_extent,
    qreal target_extent)
{
    const qreal frame = std::isfinite(frame_extent)
        ? std::max<qreal>(0.0, frame_extent)
        : 0.0;
    const qreal target = std::isfinite(target_extent)
        ? std::max<qreal>(0.0, target_extent)
        : 0.0;
    return std::max<qreal>(0.0, target - frame) * 3.0 / 11.0;
}

qreal resize_outward_extent(
    qreal frame_extent,
    qreal target_extent)
{
    const qreal frame = std::isfinite(frame_extent)
        ? std::max<qreal>(0.0, frame_extent)
        : 0.0;
    const qreal target = std::isfinite(target_extent)
        ? std::max<qreal>(0.0, target_extent)
        : 0.0;
    const qreal shortfall = std::max<qreal>(0.0, target - frame);
    return shortfall - resize_inward_extent(frame, target);
}

} // namespace vnm_qml_chrome

vnm_qml_chrome::Chrome_geometry::Chrome_geometry(QObject* parent)
:
    QObject(parent)
{
}

qreal vnm_qml_chrome::Chrome_geometry::default_resize_target_extent() const
{
    return k_default_resize_target_extent;
}

qreal vnm_qml_chrome::Chrome_geometry::normalized_device_pixel_ratio(
    qreal device_pixel_ratio) const
{
    return vnm_qml_chrome::normalized_device_pixel_ratio(device_pixel_ratio);
}

qreal vnm_qml_chrome::Chrome_geometry::snapped_logical_edge(
    qreal logical_edge,
    qreal device_pixel_ratio) const
{
    return vnm_qml_chrome::snapped_logical_edge(logical_edge, device_pixel_ratio);
}

QRectF vnm_qml_chrome::Chrome_geometry::snapped_logical_rect(
    const QRectF& logical_rect,
    qreal         device_pixel_ratio) const
{
    return vnm_qml_chrome::snapped_logical_rect(logical_rect, device_pixel_ratio);
}

bool vnm_qml_chrome::Chrome_geometry::rect_has_snapped_physical_edges(
    const QRectF& logical_rect,
    qreal         device_pixel_ratio) const
{
    return vnm_qml_chrome::rect_has_snapped_physical_edges(
        logical_rect,
        device_pixel_ratio);
}

bool vnm_qml_chrome::Chrome_geometry::physical_extent_matches_logical(
    qreal logical_extent,
    qreal physical_extent,
    qreal device_pixel_ratio) const
{
    return vnm_qml_chrome::physical_extent_matches_logical(
        logical_extent,
        physical_extent,
        device_pixel_ratio);
}

qreal vnm_qml_chrome::Chrome_geometry::physical_far_edge(
    qreal logical_edge,
    qreal physical_extent,
    qreal device_pixel_ratio) const
{
    return vnm_qml_chrome::physical_far_edge(
        logical_edge,
        physical_extent,
        device_pixel_ratio);
}

qreal vnm_qml_chrome::Chrome_geometry::resize_inward_extent(
    qreal frame_extent,
    qreal target_extent) const
{
    return vnm_qml_chrome::resize_inward_extent(frame_extent, target_extent);
}

qreal vnm_qml_chrome::Chrome_geometry::resize_outward_extent(
    qreal frame_extent,
    qreal target_extent) const
{
    return vnm_qml_chrome::resize_outward_extent(frame_extent, target_extent);
}
