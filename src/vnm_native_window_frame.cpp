#include "vnm_qml_chrome/vnm_native_window_frame.h"

#include <QEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

#include <algorithm>
#include <cmath>

namespace {

qreal non_negative_extent(qreal extent)
{
    return std::isfinite(extent) ? std::max<qreal>(0.0, extent) : 0.0;
}

#ifdef Q_OS_WIN

constexpr wchar_t k_native_frame_window_class[]  = L"VNM_NativeWindowFrameEdge";
constexpr wchar_t k_resize_border_window_class[] = L"VNM_NativeWindowResizeBorder";
constexpr int k_top_edge                         = 0;
constexpr int k_bottom_edge                      = 1;
constexpr int k_left_edge                        = 2;
constexpr int k_right_edge                       = 3;

HWND as_hwnd(void* handle)
{
    return static_cast<HWND>(handle);
}

void* from_hwnd(HWND hwnd)
{
    return static_cast<void*>(hwnd);
}

LRESULT CALLBACK native_frame_window_proc(
    HWND hwnd,
    UINT message,
    WPARAM w_param,
    LPARAM l_param)
{
    switch (message) {
        case WM_NCHITTEST:
            return HTTRANSPARENT;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT paint_struct;
            HDC dc = BeginPaint(hwnd, &paint_struct);
            if (dc) {
                RECT client_rect{};
                GetClientRect(hwnd, &client_rect);

                auto* frame = reinterpret_cast<VNM_NativeWindowFrame*>(
                    GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                const QColor frame_color = frame ? frame->frame_color() : QColor(Qt::black);
                HBRUSH brush = CreateSolidBrush(
                    RGB(frame_color.red(), frame_color.green(), frame_color.blue()));
                if (brush) {
                    FillRect(dc, &client_rect, brush);
                    DeleteObject(brush);
                }
            }
            EndPaint(hwnd, &paint_struct);
            return 0;
        }

        default:
            break;
    }

    return DefWindowProcW(hwnd, message, w_param, l_param);
}

// The ring answers with the system's sizing-border codes so Windows supplies the
// standard resize cursors; a press is still redirected to resize the owner.
LRESULT resize_border_hit_test(HWND resize_border, POINT screen_point)
{
    RECT owner_rect{};
    GetWindowRect(GetWindow(resize_border, GW_OWNER), &owner_rect);

    const bool left   = screen_point.x <  owner_rect.left;
    const bool right  = screen_point.x >= owner_rect.right;
    const bool top    = screen_point.y <  owner_rect.top;
    const bool bottom = screen_point.y >= owner_rect.bottom;
    if (top) {
        return left ? HTTOPLEFT : (right ? HTTOPRIGHT : HTTOP);
    }
    if (bottom) {
        return left ? HTBOTTOMLEFT : (right ? HTBOTTOMRIGHT : HTBOTTOM);
    }
    if (left) {
        return HTLEFT;
    }
    if (right) {
        return HTRIGHT;
    }

    // The owner grew before the ring's region caught up; the owner takes it.
    return HTTRANSPARENT;
}

Qt::Edges resize_edges(WPARAM hit_test)
{
    switch (hit_test) {
        case HTLEFT:        return Qt::LeftEdge;
        case HTRIGHT:       return Qt::RightEdge;
        case HTTOP:         return Qt::TopEdge;
        case HTBOTTOM:      return Qt::BottomEdge;
        case HTTOPLEFT:     return Qt::TopEdge    | Qt::LeftEdge;
        case HTTOPRIGHT:    return Qt::TopEdge    | Qt::RightEdge;
        case HTBOTTOMLEFT:  return Qt::BottomEdge | Qt::LeftEdge;
        case HTBOTTOMRIGHT: return Qt::BottomEdge | Qt::RightEdge;
        default:            return {};
    }
}

LRESULT CALLBACK resize_border_window_proc(
    HWND hwnd,
    UINT message,
    WPARAM w_param,
    LPARAM l_param)
{
    switch (message) {
        case WM_NCHITTEST:
            return resize_border_hit_test(
                hwnd,
                POINT{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)});

        case WM_MOUSEACTIVATE:
            // The owner keeps activation while the user grabs its resize ring.
            return MA_NOACTIVATE;

        case WM_NCLBUTTONDOWN:
        case WM_NCLBUTTONDBLCLK: {
            // Default handling would size or snap the ring itself.
            auto* frame = reinterpret_cast<VNM_NativeWindowFrame*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            frame->window()->startSystemResize(resize_edges(w_param));
            return 0;
        }

        default:
            break;
    }

    return DefWindowProcW(hwnd, message, w_param, l_param);
}

ATOM register_window_class(const wchar_t* class_name, WNDPROC window_proc)
{
    WNDCLASSEXW window_class{};
    window_class.cbSize        = sizeof(window_class);
    window_class.lpfnWndProc   = window_proc;
    window_class.hInstance     = GetModuleHandleW(nullptr);
    window_class.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = class_name;

    const ATOM window_class_atom = RegisterClassExW(&window_class);
    if (window_class_atom == 0 && GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
        return 1;
    }
    return window_class_atom;
}

ATOM ensure_native_frame_window_class()
{
    static const ATOM window_class_atom =
        register_window_class(k_native_frame_window_class, native_frame_window_proc);
    return window_class_atom;
}

ATOM ensure_resize_border_window_class()
{
    static const ATOM window_class_atom =
        register_window_class(k_resize_border_window_class, resize_border_window_proc);
    return window_class_atom;
}

// Windows destroys an owned window together with its owner and may later hand
// the same handle to an unrelated window, so a stored handle is checked first.
bool is_resize_border_window_of(void* handle, const VNM_NativeWindowFrame* frame)
{
    HWND hwnd = as_hwnd(handle);
    return
        hwnd && IsWindow(hwnd) &&
        GetWindowLongPtrW(hwnd, GWLP_USERDATA) == reinterpret_cast<LONG_PTR>(frame);
}

#endif

} // namespace

VNM_NativeWindowFrame::VNM_NativeWindowFrame(QObject* parent)
:
    QObject(parent)
{
}

VNM_NativeWindowFrame::~VNM_NativeWindowFrame()
{
#ifdef Q_OS_WIN
    destroy_resize_border_window();
    destroy_edge_windows();
#endif
    disconnect_window();
}

QWindow* VNM_NativeWindowFrame::window() const
{
    return m_window;
}

void VNM_NativeWindowFrame::set_window(QWindow* window)
{
    if (m_window == window) {
        return;
    }

#ifdef Q_OS_WIN
    destroy_resize_border_window();
    destroy_edge_windows();
#endif
    disconnect_window();

    m_window = window;
    if (m_window) {
        // A display scale change keeps a window's logical geometry and its
        // screen, so none of the signals below report it. The frame is sized in
        // physical pixels, so it has to be rebuilt anyway. Qt sends this event
        // to the window whenever its ratio actually moves.
        m_window->installEventFilter(this);
        m_window_connections.push_back(QObject::connect(
            m_window,
            &QWindow::xChanged,
            this,
            [this](int) { update_native_frame(); }));
        m_window_connections.push_back(QObject::connect(
            m_window,
            &QWindow::yChanged,
            this,
            [this](int) { update_native_frame(); }));
        m_window_connections.push_back(QObject::connect(
            m_window,
            &QWindow::widthChanged,
            this,
            [this](int) { update_native_frame(); }));
        m_window_connections.push_back(QObject::connect(
            m_window,
            &QWindow::heightChanged,
            this,
            [this](int) { update_native_frame(); }));
        m_window_connections.push_back(QObject::connect(
            m_window,
            &QWindow::visibleChanged,
            this,
            [this](bool) { update_native_frame(); }));
        m_window_connections.push_back(QObject::connect(
            m_window,
            &QWindow::visibilityChanged,
            this,
            [this](QWindow::Visibility) { update_native_frame(); }));
        m_window_connections.push_back(QObject::connect(
            m_window,
            &QWindow::screenChanged,
            this,
            [this](QScreen*) { update_native_frame(); }));
        m_window_connections.push_back(QObject::connect(
            m_window,
            &QObject::destroyed,
            this,
            [this] {
#ifdef Q_OS_WIN
                destroy_resize_border_window();
                for (void*& edge_window : m_edge_windows) {
                    edge_window = nullptr;
                }
#endif
                disconnect_window();
                m_window = nullptr;
                set_active(false);
                emit window_changed();
            }));
    }

    emit window_changed();
    update_native_frame();
}

bool VNM_NativeWindowFrame::frame_visible() const
{
    return m_frame_visible;
}

void VNM_NativeWindowFrame::set_frame_visible(bool frame_visible)
{
    if (m_frame_visible == frame_visible) {
        return;
    }

    m_frame_visible = frame_visible;
    emit frame_visible_changed();
    update_native_frame();
}

qreal VNM_NativeWindowFrame::frame_width() const
{
    return m_frame_width;
}

void VNM_NativeWindowFrame::set_frame_width(qreal frame_width)
{
    const qreal normalized_frame_width = std::isfinite(frame_width)
        ? std::max<qreal>(0.0, frame_width)
        : 0.0;

    if (qFuzzyCompare(m_frame_width + 1.0, normalized_frame_width + 1.0)) {
        return;
    }

    m_frame_width = normalized_frame_width;
    emit frame_width_changed();
    update_native_frame();
}

QColor VNM_NativeWindowFrame::frame_color() const
{
    return m_frame_color;
}

void VNM_NativeWindowFrame::set_frame_color(const QColor& frame_color)
{
    if (m_frame_color == frame_color) {
        return;
    }

    m_frame_color = frame_color;
    emit frame_color_changed();
    update_native_frame();
}

bool VNM_NativeWindowFrame::resize_enabled() const
{
    return m_resize_enabled;
}

void VNM_NativeWindowFrame::set_resize_enabled(bool resize_enabled)
{
    if (m_resize_enabled == resize_enabled) {
        return;
    }

    m_resize_enabled = resize_enabled;
    emit resize_enabled_changed();
    update_native_frame();
}

QMarginsF VNM_NativeWindowFrame::resize_outward_margins() const
{
    return m_resize_outward_margins;
}

void VNM_NativeWindowFrame::set_resize_outward_margins(
    const QMarginsF& resize_outward_margins)
{
    const QMarginsF normalized_margins(
        non_negative_extent(resize_outward_margins.left()),
        non_negative_extent(resize_outward_margins.top()),
        non_negative_extent(resize_outward_margins.right()),
        non_negative_extent(resize_outward_margins.bottom()));
    if (m_resize_outward_margins == normalized_margins) {
        return;
    }

    m_resize_outward_margins = normalized_margins;
    emit resize_outward_margins_changed();
    update_native_frame();
}

bool VNM_NativeWindowFrame::active() const
{
    return m_active;
}

bool VNM_NativeWindowFrame::eventFilter(QObject* watched, QEvent* event)
{
    if (event
        && event->type() == QEvent::DevicePixelRatioChange
        && watched == m_window) {
        update_native_frame();
    }

    return QObject::eventFilter(watched, event);
}

void VNM_NativeWindowFrame::disconnect_window()
{
    if (m_window) {
        m_window->removeEventFilter(this);
    }

    for (const QMetaObject::Connection& connection : m_window_connections) {
        QObject::disconnect(connection);
    }
    m_window_connections.clear();
}

void VNM_NativeWindowFrame::set_active(bool active)
{
    if (m_active == active) {
        return;
    }

    m_active = active;
    emit active_changed();
}

void VNM_NativeWindowFrame::update_native_frame()
{
#ifdef Q_OS_WIN
    if (!should_use_native_frame()) {
        clear_native_frame();
        set_active(false);
    }
    else {
        set_active(apply_native_frame());
    }

    update_resize_border_window();
#else
    set_active(false);
#endif
}

#ifdef Q_OS_WIN

bool VNM_NativeWindowFrame::should_use_native_frame() const
{
    return m_window
        && m_window->isVisible()
        && m_frame_visible
        && m_frame_width > 0.0
        && m_frame_color.isValid()
        && m_frame_color.alpha() == 255;
}

bool VNM_NativeWindowFrame::should_use_native_resize_border() const
{
    return m_window
        && m_window->isVisible()
        && m_window->visibility() == QWindow::Windowed
        && m_resize_enabled
        && !m_resize_outward_margins.isNull();
}

void* VNM_NativeWindowFrame::window_handle() const
{
    // Only the windows platform plugin backs a QWindow with an HWND. Other
    // plugins, such as offscreen, hand out small identifiers that IsWindow can
    // mistake for the handle of an unrelated window.
    if (!m_window || QGuiApplication::platformName() != QStringLiteral("windows")) {
        return nullptr;
    }

    return from_hwnd(reinterpret_cast<HWND>(m_window->winId()));
}

bool VNM_NativeWindowFrame::apply_native_frame()
{
    void* handle = window_handle();
    HWND hwnd    = as_hwnd(handle);
    if (!hwnd || IsIconic(hwnd)) {
        hide_edge_windows();
        return false;
    }

    if (!ensure_edge_windows(handle)) {
        hide_edge_windows();
        return false;
    }

    position_edge_windows(handle);
    repaint_edge_windows();
    return true;
}

void VNM_NativeWindowFrame::clear_native_frame()
{
    destroy_edge_windows();
}

bool VNM_NativeWindowFrame::ensure_edge_windows(void* parent_window_handle)
{
    HWND parent_hwnd = as_hwnd(parent_window_handle);
    if (!parent_hwnd || ensure_native_frame_window_class() == 0) {
        return false;
    }

    HINSTANCE instance = GetModuleHandleW(nullptr);
    for (void*& edge_window : m_edge_windows) {
        HWND hwnd = as_hwnd(edge_window);
        if (hwnd && (!IsWindow(hwnd) || GetParent(hwnd) != parent_hwnd)) {
            if (IsWindow(hwnd)) {
                DestroyWindow(hwnd);
            }
            edge_window = nullptr;
            hwnd        = nullptr;
        }

        if (hwnd) {
            continue;
        }

        hwnd = CreateWindowExW(
            WS_EX_NOACTIVATE,
            k_native_frame_window_class,
            L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0,
            0,
            0,
            0,
            parent_hwnd,
            nullptr,
            instance,
            nullptr);
        if (!hwnd) {
            destroy_edge_windows();
            return false;
        }

        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        edge_window = from_hwnd(hwnd);
    }

    return true;
}

void VNM_NativeWindowFrame::hide_edge_windows()
{
    for (void* edge_window : m_edge_windows) {
        HWND hwnd = as_hwnd(edge_window);
        if (hwnd && IsWindow(hwnd)) {
            ShowWindow(hwnd, SW_HIDE);
        }
    }
}

void VNM_NativeWindowFrame::destroy_edge_windows()
{
    for (void*& edge_window : m_edge_windows) {
        HWND hwnd = as_hwnd(edge_window);
        if (hwnd && IsWindow(hwnd)) {
            DestroyWindow(hwnd);
        }
        edge_window = nullptr;
    }
}

void VNM_NativeWindowFrame::position_edge_windows(void* parent_window_handle)
{
    HWND parent_hwnd = as_hwnd(parent_window_handle);
    if (!parent_hwnd || !IsWindow(parent_hwnd)) {
        hide_edge_windows();
        return;
    }

    RECT client_rect{};
    if (!GetClientRect(parent_hwnd, &client_rect)) {
        hide_edge_windows();
        return;
    }

    const int window_width  = static_cast<int>(client_rect.right - client_rect.left);
    const int window_height = static_cast<int>(client_rect.bottom - client_rect.top);
    if (window_width <= 0 || window_height <= 0) {
        hide_edge_windows();
        return;
    }

    const int width_px    = frame_width_px(parent_window_handle);
    const int edge_width  = std::min(width_px, window_width);
    const int edge_height = std::min(width_px, window_height);
    const UINT flags      = SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW;

    SetWindowPos(
        as_hwnd(m_edge_windows[k_top_edge]),
        HWND_TOP,
        0,
        0,
        window_width,
        edge_height,
        flags);
    SetWindowPos(
        as_hwnd(m_edge_windows[k_bottom_edge]),
        HWND_TOP,
        0,
        std::max(0, window_height - edge_height),
        window_width,
        edge_height,
        flags);
    SetWindowPos(
        as_hwnd(m_edge_windows[k_left_edge]),
        HWND_TOP,
        0,
        0,
        edge_width,
        window_height,
        flags);
    SetWindowPos(
        as_hwnd(m_edge_windows[k_right_edge]),
        HWND_TOP,
        std::max(0, window_width - edge_width),
        0,
        edge_width,
        window_height,
        flags);
}

void VNM_NativeWindowFrame::repaint_edge_windows()
{
    for (void* edge_window : m_edge_windows) {
        HWND hwnd = as_hwnd(edge_window);
        if (hwnd && IsWindow(hwnd)) {
            InvalidateRect(hwnd, nullptr, TRUE);
            UpdateWindow(hwnd);
        }
    }
}

int VNM_NativeWindowFrame::frame_width_px(void* parent_window_handle) const
{
    HWND parent_hwnd = as_hwnd(parent_window_handle);
    if (!parent_hwnd || m_frame_width <= 0.0) {
        return 0;
    }

    const UINT dpi          = GetDpiForWindow(parent_hwnd);
    const qreal scale       = dpi > 0 ? static_cast<qreal>(dpi) / 96.0 : 1.0;
    const int frame_width   = static_cast<int>(std::lround(m_frame_width * scale));
    return std::max(1, frame_width);
}

void VNM_NativeWindowFrame::update_resize_border_window()
{
    void* owner_handle = should_use_native_resize_border() ? window_handle() : nullptr;
    if (!owner_handle || !ensure_resize_border_window(owner_handle)) {
        hide_resize_border_window();
        return;
    }

    const qreal dpr  = m_window->devicePixelRatio();
    const int left   = qRound(m_resize_outward_margins.left()   * dpr);
    const int top    = qRound(m_resize_outward_margins.top()    * dpr);
    const int right  = qRound(m_resize_outward_margins.right()  * dpr);
    const int bottom = qRound(m_resize_outward_margins.bottom() * dpr);
    if (left + top + right + bottom == 0) {
        hide_resize_border_window();
        return;
    }

    RECT owner_rect{};
    GetWindowRect(as_hwnd(owner_handle), &owner_rect);
    const int width  = owner_rect.right  - owner_rect.left + left + right;
    const int height = owner_rect.bottom - owner_rect.top  + top  + bottom;

    HWND hwnd    = as_hwnd(m_resize_border_window);
    HRGN ring    = CreateRectRgn(0, 0, width, height);
    HRGN hole    = CreateRectRgn(left, top, width - right, height - bottom);
    HRGN current = CreateRectRgn(0, 0, 0, 0);
    CombineRgn(ring, ring, hole, RGN_DIFF);
    const bool region_unchanged =
        GetWindowRgn(hwnd, current) != ERROR && EqualRgn(current, ring);
    DeleteObject(current);
    DeleteObject(hole);
    if (region_unchanged) {
        DeleteObject(ring);
    }
    else {
        // The window takes ownership of the region handle.
        SetWindowRgn(hwnd, ring, TRUE);
    }

    SetWindowPos(
        hwnd,
        nullptr,
        owner_rect.left - left,
        owner_rect.top  - top,
        width,
        height,
        SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
}

bool VNM_NativeWindowFrame::ensure_resize_border_window(void* owner_window_handle)
{
    if (!is_resize_border_window_of(m_resize_border_window, this)) {
        m_resize_border_window = nullptr;
    }
    if (m_resize_border_window) {
        return true;
    }
    if (ensure_resize_border_window_class() == 0) {
        return false;
    }

    // Without a redirection surface the ring is never drawn and never needs a
    // graphics device, while its window region still receives hit tests.
    HWND hwnd = CreateWindowExW(
        WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        k_resize_border_window_class,
        L"",
        WS_POPUP,
        0,
        0,
        0,
        0,
        as_hwnd(owner_window_handle),
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    if (!hwnd) {
        return false;
    }

    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    m_resize_border_window = from_hwnd(hwnd);
    return true;
}

void VNM_NativeWindowFrame::hide_resize_border_window()
{
    if (is_resize_border_window_of(m_resize_border_window, this)) {
        ShowWindow(as_hwnd(m_resize_border_window), SW_HIDE);
    }
}

void VNM_NativeWindowFrame::destroy_resize_border_window()
{
    if (is_resize_border_window_of(m_resize_border_window, this)) {
        DestroyWindow(as_hwnd(m_resize_border_window));
    }
    m_resize_border_window = nullptr;
}

#endif
