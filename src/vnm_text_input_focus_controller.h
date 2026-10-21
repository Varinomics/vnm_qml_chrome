#pragma once

#include <QEvent>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QPointF>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>

#include <cstdint>
#include <vector>

class QPointingDevice;
class QQuickPopup;

/**
 * Enforces text-editor click-away and effective-presentation focus eligibility
 * for one QQuickWindow. The application event filter observes presses before a
 * child control can consume them. A popup belongs to the item it is parented
 * to, so a press on an editor's context menu stays within that editor. Ending
 * an editing context cancels Qt's saved popup restoration references;
 * subsequent focus requests remain ordinary Qt focus requests, including
 * requests for the same editor.
 * An optional scope restricts ownership through item and popup parents;
 * destroying that scope detaches the window. include_controls also applies
 * click-away to Qt Quick Controls, while the default remains editor-only.
 */
class VNM_Text_input_focus_controller : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QQuickWindow* window READ window WRITE set_window NOTIFY window_changed)
    Q_PROPERTY(QQuickItem* scope READ scope WRITE set_scope NOTIFY scope_changed)
    Q_PROPERTY(bool include_controls READ include_controls WRITE set_include_controls NOTIFY include_controls_changed)

public:
    explicit VNM_Text_input_focus_controller(QObject* parent = nullptr);
    ~VNM_Text_input_focus_controller() override;

    QQuickWindow* window() const { return m_window.data(); }
    void set_window(QQuickWindow* window);
    QQuickItem* scope() const { return m_scope.data(); }
    void set_scope(QQuickItem* scope);
    bool include_controls() const { return m_include_controls; }
    void set_include_controls(bool include_controls);

signals:
    void window_changed();
    void scope_changed();
    void include_controls_changed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Editor_context
    {
        QPointer<QQuickItem> focus_item;
        std::uint64_t identity = 0;
    };

    struct Editor_record
    {
        QPointer<QQuickItem> focus_item;
        QPointer<QQuickItem> boundary;
        std::uint64_t identity = 0;
        std::vector<QMetaObject::Connection> eligibility_connections;
    };

    struct Pending_popup_capture
    {
        QPointer<QQuickPopup> popup;
        std::uint64_t identity = 0;
        bool exiting = false;
        std::vector<QPointer<QQuickItem>> cancelled_items;
        std::vector<QMetaObject::Connection> connections;
    };

    struct Pending_pointer_press
    {
        const QPointingDevice* device = nullptr;
        int point_id = -1;
        QPointer<QQuickItem> press_boundary;
        std::vector<QPointer<QObject>> observed_targets;
        std::vector<Editor_context> contexts;
    };

    static bool is_text_input_boundary(const QQuickItem* item);
    QQuickItem* find_text_input_boundary(QQuickItem* focus_item) const;
    bool is_within_scope(QQuickItem* item) const;
    static bool is_descendant_or_self(QQuickItem* item, const QQuickItem* ancestor);

    void update_tracked_focus();
    bool popup_restores_item(QQuickItem* focus_item) const;
    void cancel_popup_restoration(QQuickItem* focus_item);
    void cancel_pending_popup_capture(QQuickPopup* popup, QQuickItem* focus_item);
    void scrub_pending_popup_capture(std::uint64_t identity);
    void retire_pending_popup_capture(std::uint64_t identity);
    bool focus_window_is_owned_popup() const;
    void disconnect_record(Editor_record& record);
    void rebuild_eligibility_connections(Editor_record& record);
    void rebuild_all_eligibility_connections();
    void prune_destroyed_records();
    void stop_tracking_focus();
    void clear_tracked_focus(Qt::FocusReason reason);
    void clear_focus_items(const std::vector<Editor_context>& contexts, Qt::FocusReason reason);
    void cancel_editor_pointer_gestures(QQuickItem* boundary);
    void validate_tracked_focus();
    bool tracked_focus_is_eligible(const Editor_record& record) const;
    QQuickItem* editor_boundary_at_scene_position(const QPointF& scene_position) const;
    QQuickItem* begin_pointer_press(QObject* watched, const QPointF& global_position);
    QQuickPopup* pressed_popup(QObject* watched, const QPointF& global_position) const;
    void schedule_pointer_resolution(
        const QPointingDevice* device,
        int point_id,
        QQuickItem* press_boundary);
    void resolve_pending_pointer_presses();
    QQuickItem* pointer_press_editor_boundary(const Pending_pointer_press& press) const;
    void observe_pointer_target(QQuickItem* target_item);

    QPointer<QQuickWindow> m_window;
    QPointer<QQuickItem>   m_scope;
    QMetaObject::Connection m_scope_destroyed_connection;
    bool                   m_include_controls = false;
    QPointer<QQuickItem>   m_last_active_editor;
    QTimer                 m_pointer_resolution_timer;
    std::vector<Pending_pointer_press> m_pending_pointer_presses;
    bool                   m_clear_requested_during_press = false;

    std::vector<QMetaObject::Connection> m_window_connections;
    std::vector<Editor_record>           m_editor_records;
    std::vector<Pending_popup_capture>   m_pending_popup_captures;
    std::uint64_t                       m_next_popup_capture_identity = 0;
    std::uint64_t                       m_next_editor_identity = 0;
};
