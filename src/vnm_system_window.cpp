#include "vnm_qml_chrome/vnm_system_window.h"

#include "vnm_qml_chrome/vnm_chrome_geometry.h"

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QHash>
#include <QKeyEvent>
#include <QMetaObject>
#include <QPointer>
#include <QSet>
#include <QSize>
#include <QWindow>

#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <windows.h>
#endif

#include <cmath>

namespace {

bool is_valid_resize_edges(Qt::Edges edges)
{
    switch (edges.toInt()) {
        case int(Qt::LeftEdge):
        case int(Qt::RightEdge):
        case int(Qt::TopEdge):
        case int(Qt::BottomEdge):
        case int(Qt::LeftEdge  | Qt::TopEdge):
        case int(Qt::RightEdge | Qt::TopEdge):
        case int(Qt::LeftEdge  | Qt::BottomEdge):
        case int(Qt::RightEdge | Qt::BottomEdge):
            return true;

        default:
            return false;
    }
}

bool nearly_equal(qreal lhs, qreal rhs)
{
    return std::abs(lhs - rhs) <= 0.001;
}

#ifdef Q_OS_WIN

HWND window_hwnd(QWindow* window)
{
    if (!window) {
        return nullptr;
    }

    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    return hwnd && IsWindow(hwnd) ? hwnd : nullptr;
}

QSize dwm_window_physical_size(HWND hwnd)
{
    RECT bounds{};
    const HRESULT result = DwmGetWindowAttribute(
        hwnd,
        DWMWA_EXTENDED_FRAME_BOUNDS,
        &bounds,
        sizeof(bounds));
    if (FAILED(result)) {
        return QSize();
    }

    const int width  = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0) {
        return QSize();
    }

    return QSize(width, height);
}

bool current_process_owns_foreground_window()
{
    const HWND foreground_window = GetForegroundWindow();
    if (!foreground_window) {
        return false;
    }

    DWORD foreground_process_id = 0;
    GetWindowThreadProcessId(foreground_window, &foreground_process_id);
    return foreground_process_id == GetCurrentProcessId();
}

class Foreground_lock_service : public QObject
{
public:
    explicit Foreground_lock_service(QGuiApplication* application)
    :
        QObject(application),
        m_application(application)
    {
        m_alt_pressed = QGuiApplication::queryKeyboardModifiers()
            .testFlag(Qt::AltModifier);
        application->installEventFilter(this);
        m_application_state_changed = QObject::connect(
            application,
            &QGuiApplication::applicationStateChanged,
            this,
            [this](Qt::ApplicationState) {
                // Alt+Tab delivers the release to the newly active process.
                // Resync when this process returns so a stale press cannot
                // suppress every later relock.
                m_alt_pressed = QGuiApplication::queryKeyboardModifiers()
                    .testFlag(Qt::AltModifier);
                handle_automatic_native_unlock();
            });
        m_focus_window_changed = QObject::connect(
            application,
            &QGuiApplication::focusWindowChanged,
            this,
            [this](QWindow*) {
                handle_automatic_native_unlock();
            });
    }

    void associate_direct_target(QWindow* window)
    {
        if (!window || m_direct_targets.contains(window)) {
            refresh_native_lock();
            return;
        }

        m_direct_targets.insert(window);
        add_target_reference(window);
        refresh_native_lock();
    }

    void associate_owner(QObject* owner, QWindow* window)
    {
        if (!owner) {
            return;
        }

        auto owner_it = m_owner_associations.find(owner);
        if (owner_it != m_owner_associations.end()
            && owner_it->window == window)
        {
            refresh_native_lock();
            return;
        }

        if (owner_it == m_owner_associations.end()) {
            if (!window) {
                return;
            }

            Owner_association association;
            association.window = window;
            association.owner_destroyed = QObject::connect(
                owner,
                &QObject::destroyed,
                this,
                &Foreground_lock_service::remove_owner);
            m_owner_associations.insert(owner, association);
            add_target_reference(window);
        }
        else {
            QWindow* previous_window = owner_it->window;
            if (!window) {
                const QMetaObject::Connection owner_destroyed =
                    owner_it->owner_destroyed;
                m_owner_associations.erase(owner_it);
                QObject::disconnect(owner_destroyed);
            }
            else {
                owner_it->window = window;
                add_target_reference(window);
            }

            release_target_reference(previous_window);
        }

        refresh_native_lock();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        Q_UNUSED(watched);

        if (event->type() == QEvent::KeyPress
            || event->type() == QEvent::KeyRelease)
        {
            const auto* key_event = static_cast<const QKeyEvent*>(event);
            if (key_event->key() == Qt::Key_Alt) {
                m_alt_pressed = event->type() == QEvent::KeyPress;
                if (m_alt_pressed) {
                    // Windows automatically releases the foreground lock as
                    // soon as the user presses Alt.
                    m_native_lock_owned = false;
                }
                else {
                    refresh_native_lock();
                }
            }
        }

        return QObject::eventFilter(watched, event);
    }

private:
    struct Owner_association
    {
        QWindow*                window = nullptr;
        QMetaObject::Connection owner_destroyed;
    };

    struct Target_association
    {
        qsizetype               reference_count = 0;
        QMetaObject::Connection flags_changed;
        QMetaObject::Connection visibility_changed;
        QMetaObject::Connection window_state_changed;
        QMetaObject::Connection active_changed;
        QMetaObject::Connection destroyed;
    };

    void add_target_reference(QWindow* window)
    {
        auto target_it = m_targets.find(window);
        if (target_it != m_targets.end()) {
            ++target_it->reference_count;
            return;
        }

        target_it = m_targets.insert(window, Target_association{});
        target_it->reference_count = 1;
        target_it->flags_changed = QObject::connect(
            window,
            &QWindow::flagsChanged,
            this,
            [this](Qt::WindowFlags) {
                refresh_native_lock();
            });
        target_it->visibility_changed = QObject::connect(
            window,
            &QWindow::visibilityChanged,
            this,
            [this](QWindow::Visibility) {
                refresh_native_lock();
            });
        target_it->window_state_changed = QObject::connect(
            window,
            &QWindow::windowStateChanged,
            this,
            [this](Qt::WindowState) {
                refresh_native_lock();
            });
        target_it->active_changed = QObject::connect(
            window,
            &QWindow::activeChanged,
            this,
            [this]() {
                handle_automatic_native_unlock();
            });
        target_it->destroyed = QObject::connect(
            window,
            &QObject::destroyed,
            this,
            &Foreground_lock_service::remove_target);
    }

    void release_target_reference(QWindow* window)
    {
        auto target_it = m_targets.find(window);
        if (target_it == m_targets.end()) {
            return;
        }

        --target_it->reference_count;
        if (target_it->reference_count > 0) {
            return;
        }

        const Target_association association = target_it.value();
        m_targets.erase(target_it);
        disconnect_target(association);
    }

    void remove_owner(QObject* owner)
    {
        auto owner_it = m_owner_associations.find(owner);
        if (owner_it == m_owner_associations.end()) {
            return;
        }

        const Owner_association association = owner_it.value();
        m_owner_associations.erase(owner_it);
        QObject::disconnect(association.owner_destroyed);
        release_target_reference(association.window);
        // The owner may also be a directly registered target. Its target-side
        // destroyed callback can run later in this same signal delivery, so do
        // not inspect the target table until every destruction role is gone.
        schedule_native_lock_refresh();
    }

    void remove_target(QObject* object)
    {
        auto* window = static_cast<QWindow*>(object);
        m_direct_targets.remove(window);

        for (auto owner_it = m_owner_associations.begin();
             owner_it != m_owner_associations.end();)
        {
            if (owner_it->window != window) {
                ++owner_it;
                continue;
            }

            const QMetaObject::Connection owner_destroyed =
                owner_it->owner_destroyed;
            owner_it = m_owner_associations.erase(owner_it);
            QObject::disconnect(owner_destroyed);
        }

        auto target_it = m_targets.find(window);
        if (target_it != m_targets.end()) {
            const Target_association association = target_it.value();
            m_targets.erase(target_it);
            disconnect_target(association);
        }

        refresh_native_lock();
    }

    static void disconnect_target(const Target_association& association)
    {
        QObject::disconnect(association.flags_changed);
        QObject::disconnect(association.visibility_changed);
        QObject::disconnect(association.window_state_changed);
        QObject::disconnect(association.active_changed);
        QObject::disconnect(association.destroyed);
    }

    void handle_automatic_native_unlock()
    {
        // Windows automatically enables foreground changes when the user
        // changes the foreground window. The API has no query for that state,
        // so these documented release events are authoritative.
        m_native_lock_owned = false;
        schedule_native_lock_refresh();
    }

    void schedule_native_lock_refresh()
    {
        if (m_refresh_pending) {
            return;
        }

        m_refresh_pending = true;
        QMetaObject::invokeMethod(
            this,
            [this]() {
                m_refresh_pending = false;
                refresh_native_lock();
            },
            Qt::QueuedConnection);
    }

    void refresh_native_lock()
    {
        const bool application_active =
            m_application->applicationState() == Qt::ApplicationActive;
        const bool process_is_foreground =
            current_process_owns_foreground_window();

        if (!application_active || !process_is_foreground || m_alt_pressed) {
            // Losing the foreground or pressing Alt are both documented
            // automatic-release paths. Never issue an unlock for them.
            m_native_lock_owned = false;
            return;
        }

        if (has_eligible_target()) {
            if (!m_native_lock_owned
                && LockSetForegroundWindow(LSFW_LOCK) != FALSE)
            {
                m_native_lock_owned = true;
            }
            return;
        }

        if (!m_native_lock_owned) {
            return;
        }

        if (LockSetForegroundWindow(LSFW_UNLOCK) != FALSE) {
            m_native_lock_owned = false;
        }
        else if (!current_process_owns_foreground_window()) {
            // Foreground ownership raced the unlock call. Windows released the
            // lock as part of that foreground transition.
            m_native_lock_owned = false;
        }
    }

    bool has_eligible_target() const
    {
        for (auto it = m_targets.cbegin(); it != m_targets.cend(); ++it) {
            const QWindow* window = it.key();
            if (window->flags().testFlag(Qt::WindowStaysOnTopHint) &&
                window->isVisible()                                    &&
                window->visibility() != QWindow::Minimized              &&
                !window->windowStates().testFlag(Qt::WindowMinimized))
            {
                return true;
            }
        }

        return false;
    }

    QGuiApplication*                       m_application = nullptr;
    // Direct calls live with their target. Titlebar associations live with a
    // separate owner and may be replaced; the namespaces must never collide.
    QHash<QObject*, Owner_association>     m_owner_associations;
    QHash<QWindow*, Target_association>    m_targets;
    QSet<QWindow*>                         m_direct_targets;
    QMetaObject::Connection                m_application_state_changed;
    QMetaObject::Connection                m_focus_window_changed;
    // True only after this service's latest LSFW_LOCK call succeeded.
    bool                                   m_native_lock_owned = false;
    bool                                   m_alt_pressed       = false;
    bool                                   m_refresh_pending   = false;
};

QPointer<Foreground_lock_service>& foreground_lock_service_instance()
{
    static QPointer<Foreground_lock_service> service;
    return service;
}

Foreground_lock_service* foreground_lock_service()
{
    auto* application =
        qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    if (!application) {
        return nullptr;
    }

    auto& service = foreground_lock_service_instance();
    if (!service) {
        service = new Foreground_lock_service(application);
    }
    return service;
}

#endif

} // namespace

vnm_qml_chrome::System_window::System_window(QObject* parent)
:
    QObject(parent)
{
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->installEventFilter(this);
    }

    update_alt_modifier_active();
}

vnm_qml_chrome::System_window::~System_window() = default;

qint64 vnm_qml_chrome::System_window::process_id() const
{
    return QCoreApplication::applicationPid();
}

bool vnm_qml_chrome::System_window::alt_modifier_active() const
{
    return m_alt_modifier_active;
}

bool vnm_qml_chrome::System_window::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
        case QEvent::KeyPress:
        case QEvent::KeyRelease: {
            const auto* key_event = static_cast<const QKeyEvent*>(event);
            if (key_event->key() == Qt::Key_Alt) {
                // Windows removes the modifier bit from the event state for
                // the modifier key itself ("invert state logic" in
                // qwindowskeymapper.cpp), so neither key_event->modifiers()
                // nor keyboardModifiers() can be trusted for Alt's own
                // press/release. The event type carries the truth. (AltGr is
                // deliberately not special-cased: on Windows it maps to
                // Key_Alt, and elsewhere it does not imply AltModifier.)
                // Known limitation: holding both physical Alt keys and
                // releasing one reports "not held" until the next event.
                set_alt_modifier_active(event->type() == QEvent::KeyPress);
            }
            else {
                // modifier_buttons was already updated from this event in
                // QGuiApplicationPrivate::processKeyEvent(), so the global
                // query is fresh for every non-modifier key.
                update_alt_modifier_active();
            }
            break;
        }

        case QEvent::ApplicationStateChange:
            // Alt may have been released while another application had
            // focus (e.g. Alt+Tab): that KeyRelease went elsewhere, so
            // resync from the physical keyboard state.
            sync_alt_modifier_active();
            break;

        default:
            break;
    }

    return QObject::eventFilter(watched, event);
}

void vnm_qml_chrome::System_window::set_alt_modifier_active(bool active)
{
    if (active == m_alt_modifier_active) {
        return;
    }

    m_alt_modifier_active = active;
    Q_EMIT alt_modifier_active_changed();
}

void vnm_qml_chrome::System_window::update_alt_modifier_active()
{
    if (!QGuiApplication::instance()) {
        return;
    }

    set_alt_modifier_active(
        QGuiApplication::keyboardModifiers().testFlag(Qt::AltModifier));
}

void vnm_qml_chrome::System_window::sync_alt_modifier_active()
{
    if (!QGuiApplication::instance()) {
        return;
    }

    set_alt_modifier_active(
        QGuiApplication::queryKeyboardModifiers().testFlag(Qt::AltModifier));
}

bool vnm_qml_chrome::System_window::start_system_move(QWindow* window) const
{
    if (!window) {
        return false;
    }

    return window->startSystemMove();
}

bool vnm_qml_chrome::System_window::start_system_resize(
    QWindow* window,
    int      edges) const
{
    if (!window) {
        return false;
    }

    const Qt::Edges qt_edges = Qt::Edges::fromInt(edges);
    if (!is_valid_resize_edges(qt_edges)) {
        return false;
    }

    return window->startSystemResize(qt_edges);
}

bool vnm_qml_chrome::System_window::set_window_stays_on_top(
    QWindow* window,
    bool     enabled)
{
    if (!window) {
        return false;
    }

    Qt::WindowFlags flags = window->flags();
    if (enabled) {
        flags.setFlag(Qt::WindowStaysOnBottomHint, false);
    }
    flags.setFlag(Qt::WindowStaysOnTopHint, enabled);

    if (flags != window->flags()) {
        window->setFlags(flags);
    }

    const bool effective =
        window->flags().testFlag(Qt::WindowStaysOnTopHint);
#ifdef Q_OS_WIN
    if (auto* service = foreground_lock_service()) {
        service->associate_direct_target(window);
    }
#endif
    return effective == enabled;
}

void vnm_qml_chrome::System_window::track_window_stays_on_top(
    QObject* owner,
    QWindow* window)
{
#ifdef Q_OS_WIN
    if (auto* service = foreground_lock_service()) {
        service->associate_owner(owner, window);
    }
#else
    Q_UNUSED(owner);
    Q_UNUSED(window);
#endif
}

QSize vnm_qml_chrome::System_window::native_window_physical_size(
    QWindow* window,
    qreal    logical_width,
    qreal    logical_height,
    qreal    device_pixel_ratio) const
{
#ifdef Q_OS_WIN
    if (window
        && nearly_equal(logical_width,  window->width())
        && nearly_equal(logical_height, window->height())) {
        const QSize native_size = dwm_window_physical_size(window_hwnd(window));
        if (native_size.isValid()
            && physical_extent_matches_logical(
                logical_width,
                native_size.width(),
                device_pixel_ratio)
            && physical_extent_matches_logical(
                logical_height,
                native_size.height(),
                device_pixel_ratio)) {
            return native_size;
        }
    }
#else
    Q_UNUSED(window);
#endif

    Q_UNUSED(logical_width);
    Q_UNUSED(logical_height);
    Q_UNUSED(device_pixel_ratio);
    return QSize();
}
