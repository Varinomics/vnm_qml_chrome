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

    Q_INVOKABLE qreal resize_inward_extent(
        qreal frame_extent,
        qreal target_extent = k_default_resize_target_extent) const;

    Q_INVOKABLE qreal resize_outward_extent(
        qreal frame_extent,
        qreal target_extent = k_default_resize_target_extent) const;
};

} // namespace vnm_qml_chrome
