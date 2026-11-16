#pragma once

#include <QMetaMethod>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QQmlParserStatus>
#include <QQuickItem>

#include <cstdint>
#include <vector>

/**
 * Defers Qt's automatic clipboard inspection until an existing text input can
 * be used. Editing properties and explicit clipboard operations remain Qt's.
 */
class VNM_Text_input_clipboard_notifications : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)

    Q_PROPERTY(QQuickItem* target READ target WRITE set_target NOTIFY target_changed)
    Q_PROPERTY(bool supported READ supported NOTIFY supported_changed)
    Q_PROPERTY(bool presentation_active READ presentation_active WRITE set_presentation_active
        NOTIFY presentation_active_changed)

public:
    explicit VNM_Text_input_clipboard_notifications(QObject* parent = nullptr);
    ~VNM_Text_input_clipboard_notifications() override;

    QQuickItem* target() const { return m_target.data(); }
    void set_target(QQuickItem* target);
    bool supported() const { return m_supported; }
    bool presentation_active() const { return m_presentation_active; }
    void set_presentation_active(bool active);

    void classBegin() override {}
    void componentComplete() override;

signals:
    void target_changed();
    void supported_changed();
    void presentation_active_changed();

private:
    void bind_target();
    void release_target();
    void observe_window();
    void update_eligibility();
    bool eligible() const;
    void set_supported(bool supported);

    QPointer<QQuickItem> m_target;
    QMetaMethod         m_clipboard_changed;
    QMetaMethod         m_refresh;
    QMetaObject::Connection m_notification;
    std::vector<QMetaObject::Connection> m_target_connections;
    std::vector<QMetaObject::Connection> m_window_connections;
    bool m_complete = false;
    bool m_supported = false;
    bool m_presentation_active = true;
    // Native hand-back can reenter the setter; the later request owns the result.
    std::uint64_t m_target_revision = 0;
};
