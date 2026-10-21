#include "vnm_text_input_focus_controller.h"

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QInputDevice>
#include <QList>
#include <QMouseEvent>
#include <QPointerEvent>
#include <QVariant>
#include <QWindow>
#include <QtGui/private/qpointingdevice_p.h>
#include <QtQuickTemplates2/private/qquickcombobox_p.h>
#include <QtQuickTemplates2/private/qquickmenu_p.h>
#include <QtQuickTemplates2/private/qquickoverlay_p_p.h>
#include <QtQuickTemplates2/private/qquickpopup_p.h>
#include <QtQuickTemplates2/private/qquickpopup_p_p.h>
#include <QtQuickTemplates2/private/qquickpopupitem_p_p.h>
#include <QtQuickTemplates2/private/qquickpopupwindow_p_p.h>
#include <QtQuickTemplates2/private/qquicktextarea_p_p.h>
#include <QtQuickTemplates2/private/qquicktextfield_p_p.h>

#include <algorithm>
#include <utility>

namespace {

void disconnect_all(std::vector<QMetaObject::Connection>& connections)
{
    for (const auto& connection : connections) {
        QObject::disconnect(connection);
    }
    connections.clear();
}

QQuickItem* popup_owner_item(QQuickPopup* popup)
{
    QQuickItem* const parent = popup->parentItem();
    if (qobject_cast<QQuickMenu*>(popup)) {
        return parent;
    }
    auto* const combo = qobject_cast<QQuickComboBox*>(parent);
    return combo && combo->popup() == popup ? combo : nullptr;
}

// Menus and a ComboBox's own popup continue their opener's control context.
// A generic popup form contains independent controls even when it is positioned
// relative to another control.
QQuickItem* logical_parent_item(QQuickItem* item)
{
    if (auto* const popup_item = qobject_cast<QQuickPopupItem*>(item)) {
        if (QQuickItem* const owner = popup_owner_item(QQuickPopupItemPrivate::get(popup_item)->popup)) {
            return owner;
        }
    }
    return item->parentItem();
}

void clear_saved_popup_focus(QQuickPopup* popup, QQuickItem* focus_item)
{
    auto* const popup_private = QQuickPopupPrivate::get(popup);
    if (popup_private->lastActiveFocusItem == focus_item) {
        popup_private->lastActiveFocusItem.clear();
    }
    if (auto* overlay = QQuickOverlay::overlay(popup->window(), popup->parentItem())) {
        auto* const overlay_private = QQuickOverlayPrivate::get(overlay);
        if (overlay_private->lastActiveFocusItem == focus_item) {
            overlay_private->lastActiveFocusItem.clear();
            overlay_private->lastActiveFocusItemPopup.clear();
        }
    }
}

}

VNM_Text_input_focus_controller::VNM_Text_input_focus_controller(QObject* parent)
:
    QObject(parent)
{
    m_pointer_resolution_timer.setSingleShot(true);
    m_pointer_resolution_timer.setInterval(0);
    connect(
        &m_pointer_resolution_timer,
        &QTimer::timeout,
        this,
        &VNM_Text_input_focus_controller::resolve_pending_pointer_presses);
    QCoreApplication::instance()->installEventFilter(this);
}

VNM_Text_input_focus_controller::~VNM_Text_input_focus_controller()
{
    if (auto* application = QCoreApplication::instance()) {
        application->removeEventFilter(this);
    }

    disconnect_all(m_window_connections);
    stop_tracking_focus();
}

void VNM_Text_input_focus_controller::set_window(QQuickWindow* window)
{
    if (m_window == window) {
        return;
    }

    disconnect_all(m_window_connections);
    stop_tracking_focus();
    m_window = window;

    if (m_window) {
        m_window_connections.push_back(connect(
            qGuiApp,
            &QGuiApplication::focusWindowChanged,
            this,
            [this](QWindow* focus_window) {
                if (focus_window != m_window && !focus_window_is_owned_popup()) {
                    clear_tracked_focus(Qt::ActiveWindowFocusReason);
                }
            }));
        m_window_connections.push_back(connect(
            m_window,
            &QQuickWindow::activeFocusItemChanged,
            this,
            &VNM_Text_input_focus_controller::update_tracked_focus));
        m_window_connections.push_back(connect(
            m_window,
            &QQuickWindow::activeChanged,
            this,
            [this] {
                if (!m_window->isActive() && !focus_window_is_owned_popup()) {
                    clear_tracked_focus(Qt::ActiveWindowFocusReason);
                }
            }));
        m_window_connections.push_back(connect(
            m_window,
            &QQuickWindow::visibleChanged,
            this,
            [this] {
                if (m_window->isVisible()) {
                    validate_tracked_focus();
                }
                else {
                    clear_tracked_focus(Qt::OtherFocusReason);
                }
            }));
        m_window_connections.push_back(connect(
            m_window,
            &QQuickWindow::widthChanged,
            this,
            [this] {
                if (m_window->width() > 0) {
                    validate_tracked_focus();
                }
                else {
                    clear_tracked_focus(Qt::OtherFocusReason);
                }
            }));
        m_window_connections.push_back(connect(
            m_window,
            &QQuickWindow::heightChanged,
            this,
            [this] {
                if (m_window->height() > 0) {
                    validate_tracked_focus();
                }
                else {
                    clear_tracked_focus(Qt::OtherFocusReason);
                }
            }));

        update_tracked_focus();
    }

    emit window_changed();
}

void VNM_Text_input_focus_controller::set_scope(QQuickItem* scope)
{
    if (m_scope == scope) {
        return;
    }

    disconnect(m_scope_destroyed_connection);
    stop_tracking_focus();
    m_scope = scope;
    if (m_scope) {
        m_scope_destroyed_connection = connect(m_scope, &QObject::destroyed, this, [this] {
            // A removed scope must not turn into unrestricted window tracking.
            set_window(nullptr);
            emit scope_changed();
        });
    }
    update_tracked_focus();
    emit scope_changed();
}

void VNM_Text_input_focus_controller::set_include_controls(bool include_controls)
{
    if (m_include_controls == include_controls) {
        return;
    }

    stop_tracking_focus();
    m_include_controls = include_controls;
    update_tracked_focus();
    emit include_controls_changed();
}

bool VNM_Text_input_focus_controller::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::TabletPress: {
            const auto* pointer_event = static_cast<QPointerEvent*>(event);
            if (auto* target_item = qobject_cast<QQuickItem*>(watched)) {
                observe_pointer_target(target_item);
            }
            else if (!pointer_event->points().isEmpty()) {
                m_clear_requested_during_press = false;
                m_pending_pointer_presses.clear();
                QQuickItem* const press_boundary = begin_pointer_press(
                    watched,
                    pointer_event->points().constFirst().globalPosition());
                if (press_boundary) {
                    schedule_pointer_resolution(
                        pointer_event->pointingDevice(),
                        pointer_event->points().constFirst().id(),
                        press_boundary);
                }
            }
            break;
        }
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate: {
            const auto* pointer_event = static_cast<QPointerEvent*>(event);
            const bool watches_item = qobject_cast<QQuickItem*>(watched) != nullptr;
            bool has_pressed_point = false;
            bool has_released_point = false;
            for (const QEventPoint& point : pointer_event->points()) {
                has_pressed_point |= point.state() == QEventPoint::State::Pressed;
                has_released_point |= point.state() == QEventPoint::State::Released;
            }

            if (auto* target_item = qobject_cast<QQuickItem*>(watched)) {
                observe_pointer_target(target_item);
            }
            else if (has_pressed_point) {
                m_clear_requested_during_press = false;
                m_pending_pointer_presses.clear();
            }

            for (const QEventPoint& point : pointer_event->points()) {
                if (point.state() != QEventPoint::State::Pressed) {
                    continue;
                }

                if (!watches_item) {
                    QQuickItem* const press_boundary = begin_pointer_press(
                        watched,
                        point.globalPosition());
                    if (press_boundary) {
                        schedule_pointer_resolution(
                            pointer_event->pointingDevice(),
                            point.id(),
                            press_boundary);
                    }
                }
            }
            if (has_released_point &&
                !watches_item &&
                !m_pending_pointer_presses.empty()) {
                m_pointer_resolution_timer.stop();
                resolve_pending_pointer_presses();
            }
            break;
        }
        case QEvent::MouseButtonRelease:
        case QEvent::TabletRelease:
        case QEvent::TouchEnd:
            if (!qobject_cast<QQuickItem*>(watched) &&
                !m_pending_pointer_presses.empty()) {
                m_pointer_resolution_timer.stop();
                resolve_pending_pointer_presses();
            }
            break;
        case QEvent::NonClientAreaMouseButtonPress:
            clear_tracked_focus(Qt::MouseFocusReason);
            break;
        case QEvent::ApplicationDeactivate:
            clear_tracked_focus(Qt::ActiveWindowFocusReason);
            break;
        case QEvent::Hide:
            if (watched == m_window) {
                clear_tracked_focus(Qt::OtherFocusReason);
            }
            break;
        case QEvent::FocusOut:
        case QEvent::WindowDeactivate:
            if (watched == m_window && !focus_window_is_owned_popup()) {
                clear_tracked_focus(Qt::ActiveWindowFocusReason);
            }
            break;
        default:
            break;
    }

    return QObject::eventFilter(watched, event);
}

bool VNM_Text_input_focus_controller::is_text_input_boundary(const QQuickItem* item)
{
    const QVariant marker = item->property("clear_focus_on_outside_click");

    // TextInput and TextEdit are private C++ classes in Qt Quick, so use their
    // meta-object inheritance instead of depending on private Qt headers.
    return (marker.isValid() && marker.toBool()) ||
           item->inherits("QQuickTextInput")     ||
           item->inherits("QQuickTextEdit");
}

QQuickItem* VNM_Text_input_focus_controller::find_text_input_boundary(QQuickItem* focus_item) const
{
    if (!is_within_scope(focus_item)) {
        return nullptr;
    }
    QQuickItem* boundary = nullptr;
    bool owns_editor = false;
    for (QQuickItem* item = focus_item; item; item = item->parentItem()) {
        if (m_include_controls && qobject_cast<QQuickPopupItem*>(item)) {
            auto* const popup_item = static_cast<QQuickPopupItem*>(item);
            if (owns_editor) {
                break;
            }
            // A control-owned popup's rows belong to its opener's context.
            // Treating them separately can cancel a row's press before release.
            if (QQuickItem* const owner = popup_owner_item(QQuickPopupItemPrivate::get(popup_item)->popup)) {
                if (QQuickItem* const owner_boundary = find_text_input_boundary(owner)) {
                    return owner_boundary;
                }
            }
            break;
        }
        const bool is_editor = is_text_input_boundary(item);
        if (!boundary && (is_editor || (m_include_controls && item->inherits("QQuickControl")))) {
            boundary = item;
            owns_editor = is_editor;
        }
        else
        if (m_include_controls && boundary && item->isFocusScope() &&
            item->inherits("QQuickControl") &&
            item->property("contentItem").value<QQuickItem*>() == boundary)
        {
            boundary = item;
        }
        if (boundary && !m_include_controls) {
            return boundary;
        }
        if (item == m_scope) {
            break;
        }
    }
    return boundary;
}

bool VNM_Text_input_focus_controller::is_within_scope(QQuickItem* item) const
{
    if (!m_scope) {
        return true;
    }
    for (QQuickItem* candidate = item; candidate;) {
        if (candidate == m_scope) {
            return true;
        }
        // Scope containment follows every popup's declared parent, including
        // independent forms rendered outside the scope in the window overlay.
        if (auto* const popup_item = qobject_cast<QQuickPopupItem*>(candidate)) {
            candidate = QQuickPopupItemPrivate::get(popup_item)->popup->parentItem();
        }
        else {
            candidate = candidate->parentItem();
        }
    }
    return false;
}

bool VNM_Text_input_focus_controller::is_descendant_or_self(
    QQuickItem* item,
    const QQuickItem* ancestor)
{
    for (QQuickItem* candidate = item; candidate; candidate = logical_parent_item(candidate)) {
        if (candidate == ancestor) {
            return true;
        }
    }
    return false;
}

void VNM_Text_input_focus_controller::update_tracked_focus()
{
    prune_destroyed_records();
    QQuickItem* focus_item = m_window ? m_window->activeFocusItem() : nullptr;
    QQuickItem* const editor_boundary = find_text_input_boundary(focus_item);
    if (m_include_controls && editor_boundary) {
        for (QQuickItem* item = focus_item; item && item != editor_boundary; item = item->parentItem()) {
            if (qobject_cast<QQuickPopupItem*>(item)) {
                // A control-owned popup closing retires its rows, not their
                // owner. Track that owner rather than its popup's lifetime.
                focus_item = editor_boundary;
                break;
            }
        }
    }
    const bool starts_context = editor_boundary && m_last_active_editor != focus_item;
    m_last_active_editor = editor_boundary ? focus_item : nullptr;
    if (editor_boundary) {
        const auto existing = std::find_if(
            m_editor_records.begin(), m_editor_records.end(),
            [focus_item](const Editor_record& record) { return record.focus_item == focus_item; });
        if (existing == m_editor_records.end()) {
            Editor_record record;
            record.focus_item = focus_item;
            record.boundary   = editor_boundary;
            record.identity   = ++m_next_editor_identity;
            m_editor_records.push_back(std::move(record));
            rebuild_eligibility_connections(m_editor_records.back());
        }
        else
        if (starts_context) {
            existing->identity = ++m_next_editor_identity;
        }
    }
    validate_tracked_focus();
}

bool VNM_Text_input_focus_controller::popup_restores_item(QQuickItem* focus_item) const
{
    if (!m_window || !focus_item) {
        return false;
    }
    auto* const overlay = QQuickOverlay::overlay(m_window);
    if (!overlay) {
        return false;
    }
    const auto* const overlay_private = QQuickOverlayPrivate::get(overlay);
    if (overlay_private->lastActiveFocusItem == focus_item) {
        return true;
    }
    for (QQuickPopup* popup : overlay_private->allPopups) {
        if (QQuickPopupPrivate::get(popup)->lastActiveFocusItem == focus_item) {
            return true;
        }
    }
    return false;
}

void VNM_Text_input_focus_controller::cancel_popup_restoration(QQuickItem* focus_item)
{
    if (!m_window || !focus_item) {
        return;
    }
    auto* const overlay = QQuickOverlay::overlay(m_window);
    if (!overlay) {
        return;
    }

    // Qt can rebuild the overlay's saved target from another popup when one
    // closes. Remove every copy belonging to the ended editing context.
    auto* const overlay_private = QQuickOverlayPrivate::get(overlay);
    if (overlay_private->lastActiveFocusItem == focus_item) {
        overlay_private->lastActiveFocusItem.clear();
        overlay_private->lastActiveFocusItemPopup.clear();
    }
    for (QQuickPopup* popup : overlay_private->allPopups) {
        clear_saved_popup_focus(popup, focus_item);
        const auto* const popup_private = QQuickPopupPrivate::get(popup);
        if (popup->isVisible() &&
            popup_private->transitionState != QQuickPopupPrivate::ExitTransition)
        {
            cancel_pending_popup_capture(popup, focus_item);
        }
    }
}

void VNM_Text_input_focus_controller::cancel_pending_popup_capture(
    QQuickPopup* popup,
    QQuickItem* focus_item)
{
    for (Pending_popup_capture& capture : m_pending_popup_captures) {
        if (capture.popup == popup) {
            if (std::find(capture.cancelled_items.begin(), capture.cancelled_items.end(), focus_item) ==
                capture.cancelled_items.end())
            {
                capture.cancelled_items.push_back(focus_item);
            }
            return;
        }
    }

    // prepareEnterTransition captures a local QPointer before showing a popup
    // window or emitting aboutToShow/visibleChanged. Those callbacks can end
    // editing before Qt copies the local pointer into its saved references.
    // Cancel that capture at the remainder of this open operation, including
    // an immediate close from a focus/opened callback. No editor focus request
    // is vetoed, and the cancellation ends with this operation.
    Pending_popup_capture capture;
    capture.popup    = popup;
    capture.identity = ++m_next_popup_capture_identity;
    capture.cancelled_items.push_back(focus_item);
    const auto identity = capture.identity;
    const auto scrub = [this, identity] { scrub_pending_popup_capture(identity); };
    const auto retire = [this, identity] { retire_pending_popup_capture(identity); };
    capture.connections.push_back(connect(popup->popupItem(), &QQuickItem::focusChanged, this, scrub));
    capture.connections.push_back(connect(popup, &QQuickPopup::openedChanged, this, scrub));
    capture.connections.push_back(connect(popup, &QQuickPopup::aboutToHide, this,
        [this, identity] {
            for (Pending_popup_capture& pending : m_pending_popup_captures) {
                if (pending.identity == identity) {
                    pending.exiting = true;
                    break;
                }
            }
            scrub_pending_popup_capture(identity);
        }));
    capture.connections.push_back(connect(popup, &QQuickPopup::aboutToShow, this,
        [this, identity, popup] {
            // Earlier QML hide/closed handlers can reopen synchronously before
            // our retirement callback. Qt has reset its transition state by
            // closed, so remember the exit boundary of this capture as well.
            for (const Pending_popup_capture& pending : m_pending_popup_captures) {
                if (pending.identity == identity &&
                    (pending.exiting ||
                     QQuickPopupPrivate::get(popup)->transitionState == QQuickPopupPrivate::ExitTransition))
                {
                    retire_pending_popup_capture(identity);
                    break;
                }
            }
        }));
    capture.connections.push_back(connect(popup, &QQuickPopup::visibleChanged, this,
        [this, identity, popup] {
            if (!popup->isVisible()) {
                retire_pending_popup_capture(identity);
            }
        }));
    capture.connections.push_back(connect(popup, &QQuickPopup::opened, this,
        [this, identity] {
            scrub_pending_popup_capture(identity);
            retire_pending_popup_capture(identity);
        }));
    capture.connections.push_back(connect(popup, &QQuickPopup::closed, this, retire));
    capture.connections.push_back(connect(popup, &QObject::destroyed, this, retire));
    m_pending_popup_captures.push_back(std::move(capture));
}

void VNM_Text_input_focus_controller::scrub_pending_popup_capture(std::uint64_t identity)
{
    for (const Pending_popup_capture& capture : m_pending_popup_captures) {
        if (capture.identity != identity || !capture.popup) {
            continue;
        }
        for (const QPointer<QQuickItem>& item : capture.cancelled_items) {
            if (item) {
                clear_saved_popup_focus(capture.popup, item);
            }
        }
        return;
    }
}

void VNM_Text_input_focus_controller::retire_pending_popup_capture(std::uint64_t identity)
{
    const auto capture = std::find_if(
        m_pending_popup_captures.begin(), m_pending_popup_captures.end(),
        [identity](const Pending_popup_capture& candidate) { return candidate.identity == identity; });
    if (capture != m_pending_popup_captures.end()) {
        disconnect_all(capture->connections);
        m_pending_popup_captures.erase(capture);
    }
    prune_destroyed_records();
}

bool VNM_Text_input_focus_controller::focus_window_is_owned_popup() const
{
    auto* popup_window = qobject_cast<QQuickPopupWindow*>(QGuiApplication::focusWindow());
    while (popup_window) {
        QWindow* const parent = popup_window->transientParent();
        if (parent == m_window) {
            return true;
        }
        popup_window = qobject_cast<QQuickPopupWindow*>(parent);
    }
    return false;
}

void VNM_Text_input_focus_controller::disconnect_record(Editor_record& record)
{
    disconnect_all(record.eligibility_connections);
}

void VNM_Text_input_focus_controller::rebuild_eligibility_connections(Editor_record& record)
{
    disconnect_all(record.eligibility_connections);

    for (QQuickItem* item = record.focus_item.data(); item; item = item->parentItem()) {
        record.eligibility_connections.push_back(connect(
            item,
            &QQuickItem::visibleChanged,
            this,
            &VNM_Text_input_focus_controller::validate_tracked_focus));
        record.eligibility_connections.push_back(connect(
            item,
            &QQuickItem::widthChanged,
            this,
            &VNM_Text_input_focus_controller::validate_tracked_focus));
        record.eligibility_connections.push_back(connect(
            item,
            &QQuickItem::heightChanged,
            this,
            &VNM_Text_input_focus_controller::validate_tracked_focus));
        record.eligibility_connections.push_back(connect(
            item,
            &QQuickItem::windowChanged,
            this,
            &VNM_Text_input_focus_controller::validate_tracked_focus));
        record.eligibility_connections.push_back(connect(
            item,
            &QQuickItem::parentChanged,
            this,
            [this] {
                rebuild_all_eligibility_connections();
                validate_tracked_focus();
            }));
    }
}

void VNM_Text_input_focus_controller::rebuild_all_eligibility_connections()
{
    prune_destroyed_records();
    for (Editor_record& record : m_editor_records) {
        rebuild_eligibility_connections(record);
    }
}

void VNM_Text_input_focus_controller::prune_destroyed_records()
{
    auto record = m_editor_records.begin();
    while (record != m_editor_records.end()) {
        if (record->focus_item && record->boundary &&
            (record->focus_item->hasFocus() || popup_restores_item(record->focus_item)))
        {
            ++record;
            continue;
        }

        disconnect_record(*record);
        record = m_editor_records.erase(record);
    }
}

void VNM_Text_input_focus_controller::stop_tracking_focus()
{
    for (Editor_record& record : m_editor_records) {
        disconnect_record(record);
    }
    m_editor_records.clear();
    for (Pending_popup_capture& capture : m_pending_popup_captures) {
        disconnect_all(capture.connections);
    }
    m_pending_popup_captures.clear();
    m_last_active_editor.clear();
    m_pointer_resolution_timer.stop();
    m_pending_pointer_presses.clear();
}

void VNM_Text_input_focus_controller::clear_tracked_focus(Qt::FocusReason reason)
{
    if (reason == Qt::MouseFocusReason) {
        m_clear_requested_during_press = true;
    }
    std::vector<Editor_context> contexts;
    for (const Editor_record& record : m_editor_records) {
        contexts.push_back({record.focus_item, record.identity});
    }
    m_pointer_resolution_timer.stop();
    m_pending_pointer_presses.clear();
    clear_focus_items(contexts, reason);
}

void VNM_Text_input_focus_controller::clear_focus_items(
    const std::vector<Editor_context>& contexts,
    Qt::FocusReason reason)
{
    // Disconnect and remove the ended contexts before emitting focus changes:
    // a handler may deliberately focus the same item and start a new context.
    for (const Editor_context& context : contexts) {
        const auto record = std::find_if(
            m_editor_records.begin(), m_editor_records.end(),
            [&context](const Editor_record& candidate) { return candidate.identity == context.identity; });
        if (!context.focus_item || record == m_editor_records.end()) {
            continue;
        }
        cancel_popup_restoration(context.focus_item);
        const QPointer<QQuickItem> boundary = record->boundary;
        if (boundary != context.focus_item) {
            cancel_popup_restoration(boundary);
        }
        disconnect_record(*record);
        m_editor_records.erase(record);
        if (m_last_active_editor == context.focus_item) {
            m_last_active_editor.clear();
        }
        if (boundary != context.focus_item) {
            cancel_editor_pointer_gestures(context.focus_item);
        }
        cancel_editor_pointer_gestures(boundary);
        const auto context_replaced = [&] {
            return std::any_of(
                m_editor_records.begin(), m_editor_records.end(),
                [&](const Editor_record& candidate) {
                    const bool same_component = candidate.focus_item == context.focus_item ||
                        candidate.boundary == boundary;
                    return same_component && std::none_of(contexts.begin(), contexts.end(),
                        [&](const Editor_context& ending) { return ending.identity == candidate.identity; });
                });
        };
        if (context.focus_item && !context_replaced()) {
            // Clearing a composite editor first activates its still-focused
            // scope. SpinBox immediately sends focus back to its contentItem.
            // End the owning scope before clearing its remembered child focus.
            if (boundary && boundary != context.focus_item) {
                boundary->setFocus(false, reason);
            }
            if (context.focus_item && !context_replaced()) {
                context.focus_item->setFocus(false, reason);
            }
        }
    }
    prune_destroyed_records();
}

void VNM_Text_input_focus_controller::cancel_editor_pointer_gestures(QQuickItem* boundary)
{
    struct Pointer_grabber
    {
        QPointer<QPointingDevice> device;
        QPointer<QObject> grabber;
    };

    std::vector<Pointer_grabber> grabbers;
    for (const QInputDevice* device : QInputDevice::devices()) {
        const auto* pointing_device = qobject_cast<const QPointingDevice*>(device);
        if (!pointing_device) {
            continue;
        }
        const auto* device_private = QPointingDevicePrivate::get(pointing_device);
        const auto append_grabber = [&](QObject* grabber) {
            if (!grabber) {
                return;
            }
            auto* item = qobject_cast<QQuickItem*>(grabber);
            if (!item) {
                item = qobject_cast<QQuickItem*>(grabber->parent());
            }
            if (is_descendant_or_self(item, boundary) &&
                std::none_of(grabbers.begin(), grabbers.end(),
                    [&](const Pointer_grabber& candidate) {
                        return candidate.device == pointing_device && candidate.grabber == grabber;
                    }))
            {
                grabbers.push_back({const_cast<QPointingDevice*>(pointing_device), grabber});
            }
        };
        for (const auto& point : device_private->activePoints.values()) {
            append_grabber(point.exclusiveGrabber);
            for (const QPointer<QObject>& grabber : point.passiveGrabbers) {
                append_grabber(grabber);
            }
        }
    }
    // TextField/TextArea replay their delayed press when release arrives;
    // TextInput also focuses on release on touch-release-focus platforms.
    // End the owning gesture when editing ends. Qt's ungrab notification
    // resets selection, but does not stop the controls' press-and-hold timer.
    QQuickPressHandler* press_handler = nullptr;
    if (auto* field = qobject_cast<QQuickTextField*>(boundary)) {
        press_handler = &QQuickTextFieldPrivate::get(field)->pressHandler;
    }
    else
    if (auto* area = qobject_cast<QQuickTextArea*>(boundary)) {
        press_handler = &QQuickTextAreaPrivate::get(area)->pressHandler;
    }
    if (press_handler) {
        press_handler->timer.stop();
        press_handler->delayedMousePressEvent.reset();
        press_handler->longPress = false;
    }

    // Cancellation invokes item/handler callbacks. Snapshot owners first and
    // let Qt deliver its cancellation notifications, preserving other grabs.
    for (const Pointer_grabber& grabber : grabbers) {
        if (grabber.device && grabber.grabber) {
            QPointingDevicePrivate::get(grabber.device)->removeGrabber(grabber.grabber, true);
        }
    }
}

void VNM_Text_input_focus_controller::validate_tracked_focus()
{
    prune_destroyed_records();
    std::vector<Editor_context> ineligible_items;
    for (const Editor_record& record : m_editor_records) {
        if (!tracked_focus_is_eligible(record)) {
            ineligible_items.push_back({record.focus_item, record.identity});
        }
    }
    clear_focus_items(ineligible_items, Qt::OtherFocusReason);
}

bool VNM_Text_input_focus_controller::tracked_focus_is_eligible(
    const Editor_record& record) const
{
    if (!m_window || !record.focus_item || !record.boundary) {
        return false;
    }
    if (!m_window->isVisible() || m_window->width() <= 0 || m_window->height() <= 0) {
        return false;
    }
    if (record.focus_item->window() != m_window) {
        return false;
    }
    if (!is_within_scope(record.focus_item)) {
        return false;
    }

    for (QQuickItem* item = record.focus_item.data(); item; item = item->parentItem()) {
        if (!item->isVisible() || item->width() <= 0 || item->height() <= 0) {
            return false;
        }
    }
    return true;
}

QQuickItem* VNM_Text_input_focus_controller::editor_boundary_at_scene_position(
    const QPointF& scene_position) const
{
    const auto contains_scene_position = [&scene_position](QQuickItem* boundary) {
        if (!boundary || !boundary->contains(boundary->mapFromScene(scene_position))) {
            return false;
        }

        for (QQuickItem* ancestor = boundary->parentItem();
             ancestor;
             ancestor = ancestor->parentItem())
        {
            if (ancestor->clip() && !ancestor->contains(ancestor->mapFromScene(scene_position))) {
                return false;
            }
        }
        return true;
    };

    for (const Editor_record& record : m_editor_records) {
        if (record.focus_item == m_last_active_editor &&
            contains_scene_position(record.boundary)) {
            return record.boundary;
        }
    }

    for (const Editor_record& record : m_editor_records) {
        QQuickItem* const boundary = record.boundary;
        if (contains_scene_position(boundary)) {
            return boundary;
        }
    }
    return nullptr;
}

QQuickItem* VNM_Text_input_focus_controller::begin_pointer_press(
    QObject* watched,
    const QPointF& global_position)
{
    if (m_editor_records.empty()) {
        return nullptr;
    }

    validate_tracked_focus();
    if (m_editor_records.empty()) {
        return nullptr;
    }

    // A press on a popup that belongs to an editor, such as the editor's
    // context menu, stays within that editor's editing context wherever the
    // popup is drawn: over other items, outside the editor, or in its own window.
    if (QQuickPopup* const popup = pressed_popup(watched, global_position)) {
        for (const Editor_record& record : m_editor_records) {
            if (is_descendant_or_self(popup->popupItem(), record.boundary)) {
                return record.boundary;
            }
        }
    }

    auto* const event_window = qobject_cast<QWindow*>(watched);
    if (!event_window || event_window != m_window) {
        clear_tracked_focus(Qt::MouseFocusReason);
        return nullptr;
    }

    const QPointF window_position = m_window->mapFromGlobal(global_position);
    QQuickItem* const geometric_boundary = editor_boundary_at_scene_position(window_position);
    if (!geometric_boundary) {
        clear_tracked_focus(Qt::MouseFocusReason);
        return nullptr;
    }

    return geometric_boundary;
}

QQuickPopup* VNM_Text_input_focus_controller::pressed_popup(
    QObject* watched,
    const QPointF& global_position) const
{
    if (auto* const popup_window = qobject_cast<QQuickPopupWindow*>(watched)) {
        return popup_window->popup();
    }
    if (watched != m_window) {
        return nullptr;
    }

    // The application filter sees the press before the overlay delivers it, so
    // the receiver is the topmost visible overlay child under the press: a
    // popup's item, or a modal popup's dimmer, which is not a press on a popup.
    const QPointF scene_position = m_window->mapFromGlobal(global_position);
    const QList<QQuickItem*> overlay_children =
        QQuickItemPrivate::get(QQuickOverlay::overlay(m_window))->paintOrderChildItems();
    for (auto child = overlay_children.crbegin(); child != overlay_children.crend(); ++child) {
        QQuickItem* const item = *child;
        if (!item->isVisible() || !item->contains(item->mapFromScene(scene_position))) {
            continue;
        }

        auto* const popup = qobject_cast<QQuickPopup*>(item->parent());
        return popup && popup->popupItem() == item ? popup : nullptr;
    }
    return nullptr;
}

void VNM_Text_input_focus_controller::schedule_pointer_resolution(
    const QPointingDevice* device,
    int point_id,
    QQuickItem* press_boundary)
{
    if (!device || !press_boundary) {
        return;
    }

    Pending_pointer_press press;
    press.device         = device;
    press.point_id       = point_id;
    press.press_boundary = press_boundary;
    for (const Editor_record& record : m_editor_records) {
        press.contexts.push_back({record.focus_item, record.identity});
    }
    m_pending_pointer_presses.push_back(std::move(press));
    m_pointer_resolution_timer.start();
}

void VNM_Text_input_focus_controller::resolve_pending_pointer_presses()
{
    const auto pending_presses = std::exchange(m_pending_pointer_presses, {});
    QQuickItem* preserved_boundary = nullptr;
    bool clear_all = m_clear_requested_during_press;
    for (const Pending_pointer_press& press : pending_presses) {
        QQuickItem* const point_boundary = pointer_press_editor_boundary(press);
        if (!point_boundary || (preserved_boundary && preserved_boundary != point_boundary)) {
            clear_all = true;
        }
        preserved_boundary = point_boundary;
    }

    // Only the contexts present before this press are click-away candidates.
    // A pressed handler may already have deliberately focused another editor.
    std::vector<Editor_context> contexts;
    for (const Pending_pointer_press& press : pending_presses) {
        for (const Editor_context& context : press.contexts) {
            if (context.focus_item &&
                (clear_all || find_text_input_boundary(context.focus_item) != preserved_boundary))
            {
                contexts.push_back(context);
            }
        }
    }
    m_clear_requested_during_press = false;
    clear_focus_items(contexts, Qt::MouseFocusReason);
}

QQuickItem* VNM_Text_input_focus_controller::pointer_press_editor_boundary(
    const Pending_pointer_press& press) const
{
    QList<QObject*> targets;
    bool has_point_record = false;
    if (press.device) {
        const auto* const device_private = QPointingDevicePrivate::get(press.device);
        if (const auto* const point_data = device_private->queryPointById(press.point_id)) {
            has_point_record = true;
            if (point_data->exclusiveGrabber) {
                targets.push_back(point_data->exclusiveGrabber);
            }
        }
    }

    // The application filter sees QQuickItems before they get a chance to
    // accept or reject the pointer event. Once Qt has recorded the point,
    // its exclusive grabber is authoritative: passive observers do not take
    // ownership away from the editor. A rejected overlay can remain in
    // observed_targets even when the editor ultimately owns the press.
    // Observed targets are only a fallback for platforms/event paths where Qt
    // no longer exposes a point record by the time this resolution runs.
    if (!has_point_record) {
        for (const QPointer<QObject>& target : press.observed_targets) {
            if (target) {
                targets.push_back(target);
            }
        }
    }

    QQuickItem* resolved_boundary = nullptr;
    for (QObject* target : targets) {
        QQuickItem* target_item = qobject_cast<QQuickItem*>(target);
        if (!target_item) {
            target_item = qobject_cast<QQuickItem*>(target->parent());
        }

        QQuickItem* grabber_boundary = nullptr;
        for (const Editor_record& record : m_editor_records) {
            if (is_descendant_or_self(target_item, record.boundary)) {
                grabber_boundary = record.boundary;
                break;
            }
        }
        if (!grabber_boundary ||
            (resolved_boundary && resolved_boundary != grabber_boundary)) {
            return nullptr;
        }
        resolved_boundary = grabber_boundary;
    }
    if (resolved_boundary) {
        return resolved_boundary;
    }
    return press.press_boundary;
}

void VNM_Text_input_focus_controller::observe_pointer_target(QQuickItem* target_item)
{
    if (m_pending_pointer_presses.empty() || !target_item) {
        return;
    }

    for (Pending_pointer_press& press : m_pending_pointer_presses) {
        press.observed_targets.push_back(target_item);
    }
}
