#include "vnm_text_input_clipboard_notifications.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQuickWindow>
#include <QThread>
#include <QtQuick/private/qquickwindow_p.h>

namespace {

Q_LOGGING_CATEGORY(lc_clipboard_notifications, "vnm.qml_chrome.clipboard", QtInfoMsg)

void disconnect_all(std::vector<QMetaObject::Connection>& connections)
{
    for (const auto& connection : connections) {
        QObject::disconnect(connection);
    }
    connections.clear();
}

} // namespace

VNM_Text_input_clipboard_notifications::VNM_Text_input_clipboard_notifications(QObject* parent)
:
    QObject(parent)
{}

VNM_Text_input_clipboard_notifications::~VNM_Text_input_clipboard_notifications()
{
    release_target();
}

void VNM_Text_input_clipboard_notifications::set_target(QQuickItem* target)
{
    const auto revision = ++m_target_revision;
    // A pointer still names the old input during native hand-back, but its
    // observers are already released. A later request must reacquire ownership.
    if (m_target == target && (m_supported || !m_complete)) {
        return;
    }
    const QPointer<VNM_Text_input_clipboard_notifications> guard(this);
    const QPointer<QQuickItem> requested_target(target);
    const bool was_supported = m_supported;
    release_target();
    if (!guard || m_target_revision != revision) {
        return;
    }
    m_target = requested_target;
    if (m_complete) {
        bind_target();
    }
    if (guard) {
        if (m_supported != was_supported) {
            emit supported_changed();
        }
        if (guard) {
            emit target_changed();
        }
    }
}

void VNM_Text_input_clipboard_notifications::set_presentation_active(bool active)
{
    if (m_presentation_active == active) {
        return;
    }
    m_presentation_active = active;
    const QPointer<VNM_Text_input_clipboard_notifications> guard(this);
    update_eligibility();
    if (guard) {
        emit presentation_active_changed();
    }
}

void VNM_Text_input_clipboard_notifications::componentComplete()
{
    m_complete = true;
    const QPointer<VNM_Text_input_clipboard_notifications> guard(this);
    bind_target();
    if (guard && m_supported) {
        emit supported_changed();
    }
}

void VNM_Text_input_clipboard_notifications::set_supported(bool supported)
{
    if (m_supported == supported) {
        return;
    }
    m_supported = supported;
    emit supported_changed();
}

void VNM_Text_input_clipboard_notifications::bind_target()
{
    if (!m_target) {
        return;
    }
    Q_ASSERT(m_target->thread() == QThread::currentThread());
    auto* const clipboard = QGuiApplication::clipboard();
    const int refresh_index = m_target->metaObject()->indexOfSlot("q_canPasteChanged()");
    if (!clipboard || refresh_index < 0) {
        qCDebug(lc_clipboard_notifications) << "Text input clipboard notification slot is unavailable";
        return;
    }
    m_clipboard_changed = QMetaMethod::fromSignal(&QClipboard::dataChanged);
    m_refresh = m_target->metaObject()->method(refresh_index);
    // Qt connects this private slot even for hidden inputs. Taking only that
    // connection preserves other clipboard subscribers and all editing state.
    if (!QObject::disconnect(clipboard, m_clipboard_changed, m_target, m_refresh)) {
        qCDebug(lc_clipboard_notifications) << "Text input clipboard notification connection is unavailable";
        return;
    }
    m_supported = true;
    m_target_connections.push_back(connect(
        m_target, &QQuickItem::visibleChanged, this,
        &VNM_Text_input_clipboard_notifications::update_eligibility));
    m_target_connections.push_back(connect(
        m_target, &QQuickItem::enabledChanged, this,
        &VNM_Text_input_clipboard_notifications::update_eligibility));
    m_target_connections.push_back(connect(
        m_target, &QQuickItem::windowChanged, this,
        &VNM_Text_input_clipboard_notifications::observe_window));
    m_target_connections.push_back(connect(m_target, &QObject::destroyed, this, [this] {
        const bool was_supported = m_supported;
        const QPointer<VNM_Text_input_clipboard_notifications> guard(this);
        release_target();
        if (was_supported) {
            emit supported_changed();
        }
        if (guard) {
            emit target_changed();
        }
    }));
    observe_window();
}

void VNM_Text_input_clipboard_notifications::release_target()
{
    disconnect_all(m_target_connections);
    disconnect_all(m_window_connections);
    const bool suspended = m_supported && m_target && !m_notification;
    const QPointer<QQuickItem> target = m_target;
    const QMetaMethod changed = m_clipboard_changed;
    const QMetaMethod refresh = m_refresh;
    m_notification = {};
    m_supported = false;
    if (!suspended) {
        return;
    }
    // Commit hand-back before native access can pump events. The surviving
    // input needs both Qt's subscription and a fresh cache without our observers.
    const auto notification = QObject::connect(
        QGuiApplication::clipboard(), changed, target, refresh,
        Qt::ConnectionType(Qt::DirectConnection | Qt::UniqueConnection));
    if (!notification || !refresh.invoke(target, Qt::DirectConnection)) {
        qCDebug(lc_clipboard_notifications) << "Text input clipboard ownership could not be restored";
    }
}

void VNM_Text_input_clipboard_notifications::observe_window()
{
    disconnect_all(m_window_connections);
    if (m_target && m_target->window()) {
        auto* const window = m_target->window();
        m_window_connections.push_back(connect(
            window, &QWindow::visibilityChanged, this,
            &VNM_Text_input_clipboard_notifications::update_eligibility));
    }
    update_eligibility();
}

bool VNM_Text_input_clipboard_notifications::eligible() const
{
    if (!m_presentation_active || !m_target || !m_target->isVisible() || !m_target->isEnabled()) {
        return false;
    }
    auto* const window = m_target->window();
    if (!window) {
        return false;
    }
    // Render-control windows remain natively hidden while their logical scene
    // is interactive. The scene root owns presentation visibility there.
    return QQuickWindowPrivate::get(window)->renderControl ||
        (window->isVisible() && window->visibility() != QWindow::Minimized);
}

void VNM_Text_input_clipboard_notifications::update_eligibility()
{
    if (!m_supported) {
        return;
    }
    if (!eligible()) {
        QObject::disconnect(m_notification);
        m_notification = {};
        return;
    }
    if (m_notification) {
        return;
    }
    m_notification = QObject::connect(
        QGuiApplication::clipboard(), m_clipboard_changed, m_target, m_refresh,
        Qt::ConnectionType(Qt::DirectConnection | Qt::UniqueConnection));
    if (!m_notification) {
        set_supported(false);
        qCDebug(lc_clipboard_notifications) << "Text input clipboard notification connection could not be restored";
        return;
    }
    // Hidden clipboard changes leave the cached canPaste value untouched.
    // Refresh before the caller can focus or deliver input to a shown editor.
    const QPointer<VNM_Text_input_clipboard_notifications> guard(this);
    const QPointer<QQuickItem> target = m_target;
    const QMetaMethod refresh = m_refresh;
    const bool refreshed = refresh.invoke(target, Qt::DirectConnection);
    if (guard && m_target == target && !refreshed) {
        set_supported(false);
        qCDebug(lc_clipboard_notifications) << "Text input clipboard availability could not be refreshed";
    }
}
