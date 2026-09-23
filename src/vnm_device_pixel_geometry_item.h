#pragma once

#include <QMetaObject>
#include <QPointer>
#include <QQuickItem>

#include <vector>

class QEvent;
class QQuickWindow;

class VNM_Device_pixel_geometry_item : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(qreal device_pixel_ratio
               READ device_pixel_ratio
               NOTIFY device_pixel_ratio_changed)
    Q_PROPERTY(bool snapping_active
               READ snapping_active
               NOTIFY snapping_active_changed)

public:
    explicit VNM_Device_pixel_geometry_item(QQuickItem* parent = nullptr);
    ~VNM_Device_pixel_geometry_item() override;

    qreal device_pixel_ratio() const;
    bool snapping_active() const;

signals:
    void device_pixel_ratio_changed();
    void snapping_active_changed();
    void scene_graph_recreated();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void geometryChange(const QRectF& new_geometry, const QRectF& old_geometry) override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;

private:
    struct scene_mapping_t
    {
        QPointF origin;
        qreal scale = 1.0;
        bool supported = false;
    };

    scene_mapping_t scene_mapping() const;
    qreal current_device_pixel_ratio() const;
    void apply_corrected_geometry();
    void apply_logical_fallback();
    void schedule_corrected_geometry();
    void refresh_scene_observers();
    void clear_scene_observers();
    void reconnect_window(QQuickWindow* window);
    void update_reported_state(qreal dpr, bool active);

    qreal m_device_pixel_ratio = 1.0;
    bool m_snapping_active = false;
    bool m_applying_geometry = false;
    bool m_update_queued = false;
    bool m_scene_graph_lost = false;
    std::vector<QMetaObject::Connection> m_scene_connections;
    QMetaObject::Connection m_window_connection;
    QMetaObject::Connection m_window_dpr_connection;
    QMetaObject::Connection m_window_invalidated_connection;
    QMetaObject::Connection m_window_initialized_connection;
    QPointer<QQuickWindow> m_observed_window;
};
