#include "vnm_qml_chrome/vnm_system_window.h"

#include "vnm_qml_chrome/vnm_chrome_geometry.h"

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
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
