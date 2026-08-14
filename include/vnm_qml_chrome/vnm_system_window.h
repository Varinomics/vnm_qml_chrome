#pragma once

#include <QObject>
#include <QSize>

class QWindow;

namespace vnm_qml_chrome {

/**
 * @brief QML-facing wrapper for native QWindow move and resize operations.
 *
 * QWindow requires system move and resize requests to be made from the mouse
 * event handler that starts the operation. This singleton exposes those
 * operations together with physical window-size conversion. Resize edges must
 * be one edge or two adjacent edges.
 */
class System_window : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qint64 process_id READ process_id CONSTANT)
    Q_PROPERTY(
        bool alt_modifier_active
        READ alt_modifier_active
        NOTIFY alt_modifier_active_changed)

public:
    explicit System_window(QObject* parent = nullptr);

    qint64 process_id() const;
    bool alt_modifier_active() const;

    Q_INVOKABLE bool start_system_move(QWindow* window) const;
    Q_INVOKABLE bool start_system_resize(QWindow* window, int edges) const;
    Q_INVOKABLE QSize native_window_physical_size(
        QWindow* window,
        qreal    logical_width,
        qreal    logical_height) const;

Q_SIGNALS:
    void alt_modifier_active_changed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void set_alt_modifier_active(bool active);
    void update_alt_modifier_active();
    void sync_alt_modifier_active();

    bool m_alt_modifier_active = false;
};

} // namespace vnm_qml_chrome
