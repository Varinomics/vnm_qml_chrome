#pragma once

#include <QColor>
#include <QList>
#include <QMarginsF>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QWindow>
#include <QtGlobal>

#ifdef Q_OS_WIN
#include <array>
#endif

class QQuickWindow;

/**
 * @brief Optional native border controller for frameless QWindow instances.
 *
 * On Windows, the controller can draw a four-edge child-window border and add
 * an invisible resize ring outside the attached frameless window. The visual
 * frame is active only while frame_visible is true, frame_width is positive,
 * and frame_color is valid and fully opaque. On other platforms, active remains
 * false so callers can use their QML fallback frame. Window-state policy remains
 * with the caller: fullscreen, maximized, or other state rules must be encoded
 * in frame_visible and resize_enabled.
 */
class VNM_NativeWindowFrame : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QWindow* window READ window WRITE set_window NOTIFY window_changed)
    Q_PROPERTY(bool frame_visible READ frame_visible WRITE set_frame_visible NOTIFY frame_visible_changed)
    Q_PROPERTY(qreal frame_width READ frame_width WRITE set_frame_width NOTIFY frame_width_changed)
    Q_PROPERTY(QColor frame_color READ frame_color WRITE set_frame_color NOTIFY frame_color_changed)
    Q_PROPERTY(bool resize_enabled READ resize_enabled WRITE set_resize_enabled NOTIFY resize_enabled_changed)
    Q_PROPERTY(QMarginsF resize_outward_margins
               READ resize_outward_margins
               WRITE set_resize_outward_margins
               NOTIFY resize_outward_margins_changed)
    Q_PROPERTY(bool active READ active NOTIFY active_changed)

public:
    explicit VNM_NativeWindowFrame(QObject* parent = nullptr);
    ~VNM_NativeWindowFrame() override;

    QWindow* window() const;
    void set_window(QWindow* window);

    bool frame_visible() const;
    void set_frame_visible(bool frame_visible);

    qreal frame_width() const;
    void set_frame_width(qreal frame_width);

    QColor frame_color() const;
    void set_frame_color(const QColor& frame_color);

    bool resize_enabled() const;
    void set_resize_enabled(bool resize_enabled);

    QMarginsF resize_outward_margins() const;
    void set_resize_outward_margins(const QMarginsF& resize_outward_margins);

    /**
     * @brief Return whether the native platform border is currently drawing.
     */
    bool active() const;

signals:
    void window_changed();
    void frame_visible_changed();
    void frame_width_changed();
    void frame_color_changed();
    void resize_enabled_changed();
    void resize_outward_margins_changed();
    void active_changed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void disconnect_window();
    void set_active(bool active);
    void update_native_frame();

#ifdef Q_OS_WIN
    bool should_use_native_frame() const;
    bool should_use_native_resize_border() const;
    void* window_handle() const;
    bool ensure_edge_windows(void* parent_window_handle);
    bool apply_native_frame();
    void clear_native_frame();
    void hide_edge_windows();
    void destroy_edge_windows();
    void position_edge_windows(void* parent_window_handle);
    void repaint_edge_windows();
    int frame_width_px(void* parent_window_handle) const;
    void update_resize_border_window();
    void destroy_resize_border_window();

    std::array<void*, 4> m_edge_windows{};
    QQuickWindow*        m_resize_border_window = nullptr;
#endif

    QPointer<QWindow>               m_window;
    QList<QMetaObject::Connection> m_window_connections;
    bool                            m_frame_visible          = true;
    qreal                           m_frame_width            = 1.0;
    QColor                          m_frame_color            = Qt::black;
    bool                            m_resize_enabled         = false;
    QMarginsF                       m_resize_outward_margins;
    bool                            m_active                 = false;
};
