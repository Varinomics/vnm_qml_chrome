#pragma once

#include <QObject>
#include <QRectF>
#include <QtGlobal>

namespace vnm_qml_chrome {

/** @brief Default total resize target extent in logical pixels. */
inline constexpr qreal k_default_resize_target_extent = 11.0;

/**
 * @brief Return a usable device-pixel ratio for logical-to-physical snapping.
 *
 * Non-finite and non-positive values are treated as DPR 1.0.
 */
qreal normalized_device_pixel_ratio(qreal device_pixel_ratio);

/**
 * @brief Snap a logical coordinate to the nearest integer physical pixel.
 */
qreal snapped_logical_edge(
    qreal logical_edge,
    qreal device_pixel_ratio);

/**
 * @brief Snap all rectangle edges to the nearest integer physical pixels.
 */
QRectF snapped_logical_rect(
    const QRectF& logical_rect,
    qreal         device_pixel_ratio);

/**
 * @brief Return whether every rectangle edge lands on an integer physical pixel.
 */
bool rect_has_snapped_physical_edges(
    const QRectF& logical_rect,
    qreal         device_pixel_ratio);

/**
 * @brief Largest logical disagreement a usable physical extent may show.
 *
 * A physical extent is the logical extent rounded to whole device pixels, so a
 * matching pair can only differ by half a device pixel. One logical pixel of
 * headroom accepts every consistent pair and rejects a pair sampled at two
 * different ratios.
 */
inline constexpr qreal k_physical_extent_agreement_tolerance = 1.0;

/**
 * @brief Return whether a physical extent describes the given logical extent.
 *
 * A display scale change moves the device-pixel ratio and every physical size
 * reported by the system together. Code that samples the two independently can
 * hold one from before the change and one from after, and their quotient is
 * then neither extent. This predicate rejects such a pair.
 */
bool physical_extent_matches_logical(
    qreal logical_extent,
    qreal physical_extent,
    qreal device_pixel_ratio);

/**
 * @brief Return a far frame edge, refined by a physical extent when usable.
 *
 * The logical extent owns the layout. A physical extent only refines the edge
 * onto a whole device pixel, so it is honoured only while it still describes
 * the logical extent. A physical extent left over from another ratio refines
 * nothing and is discarded, which keeps a stale ratio from rescaling the frame
 * instead of nudging its edge.
 */
qreal physical_far_edge(
    qreal logical_edge,
    qreal physical_extent,
    qreal device_pixel_ratio);

/**
 * @brief Return the content-side share of a frame-aware resize target.
 *
 * The complete frame participates in resizing. Any shortfall from the target
 * keeps the default 3:8 inward-to-outward bias.
 */
qreal resize_inward_extent(
    qreal frame_extent,
    qreal target_extent = k_default_resize_target_extent);

/**
 * @brief Return the outside share of a frame-aware resize target.
 */
qreal resize_outward_extent(
    qreal frame_extent,
    qreal target_extent = k_default_resize_target_extent);

class Chrome_geometry : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qreal default_resize_target_extent READ default_resize_target_extent CONSTANT)

public:
    explicit Chrome_geometry(QObject* parent = nullptr);

    qreal default_resize_target_extent() const;

    Q_INVOKABLE qreal normalized_device_pixel_ratio(qreal device_pixel_ratio) const;

    Q_INVOKABLE qreal snapped_logical_edge(
        qreal logical_edge,
        qreal device_pixel_ratio) const;

    Q_INVOKABLE QRectF snapped_logical_rect(
        const QRectF& logical_rect,
        qreal         device_pixel_ratio) const;

    Q_INVOKABLE bool rect_has_snapped_physical_edges(
        const QRectF& logical_rect,
        qreal         device_pixel_ratio) const;

    Q_INVOKABLE bool physical_extent_matches_logical(
        qreal logical_extent,
        qreal physical_extent,
        qreal device_pixel_ratio) const;

    Q_INVOKABLE qreal physical_far_edge(
        qreal logical_edge,
        qreal physical_extent,
        qreal device_pixel_ratio) const;

    Q_INVOKABLE qreal resize_inward_extent(
        qreal frame_extent,
        qreal target_extent = k_default_resize_target_extent) const;

    Q_INVOKABLE qreal resize_outward_extent(
        qreal frame_extent,
        qreal target_extent = k_default_resize_target_extent) const;
};

} // namespace vnm_qml_chrome
