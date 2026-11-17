#include "vnm_qml_chrome/vnm_qml_chrome_runtime.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMetaType>
#include <QMimeData>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QStringList>
#include <QUrl>
#include <QVariant>
#include <QVariantMap>
#include <QtTest/QTest>

#include <functional>
#include <memory>
#include <utility>

namespace {

struct Clipboard_observation
{
    int probes = 0;
    bool has_text = true;
    std::function<void()> on_probe;
};

class Observed_clipboard_data : public QMimeData
{
public:
    explicit Observed_clipboard_data(std::shared_ptr<Clipboard_observation> observation)
    :
        m_observation(std::move(observation))
    {}

    QStringList formats() const override
    {
        observe();
        return m_observation->has_text
            ? QStringList{QStringLiteral("text/plain")}
            : QStringList{};
    }

protected:
    QVariant retrieveData(const QString& format, QMetaType) const override
    {
        observe();
        return format == QStringLiteral("text/plain") && m_observation->has_text
            ? QVariant(QStringLiteral("clipboard text"))
            : QVariant{};
    }

private:
    void observe() const
    {
        ++m_observation->probes;
        const auto callback = std::exchange(m_observation->on_probe, {});
        if (callback) {
            callback();
        }
    }

    std::shared_ptr<Clipboard_observation> m_observation;
};

class Input_scene
{
public:
    explicit Input_scene(bool offscreen, const QString& input_type = QStringLiteral("TextInput"))
    {
        if (!vnm_init_qml_chrome_runtime(m_engine)) {
            m_error = QStringLiteral("Chrome runtime initialization failed");
            return;
        }
        if (offscreen) {
            m_render_control = std::make_unique<QQuickRenderControl>();
        }
        m_window = std::make_unique<QQuickWindow>(m_render_control.get());
        m_window->resize(360, 180);
        QQmlComponent component(&m_engine);
        component.setData(QStringLiteral(R"(
import QtQuick
import QtQuick.Controls.Basic
import VNM_Chrome

Item {
    id: m_root
    width: 360
    height: 180
    property bool input_read_only: false

    %1 {
        id: input
        objectName: "input"
        width: 320
        height: 60
        text: "alpha beta"
        readOnly: m_root.input_read_only

        VNM_TextInputClipboardNotifications {
            objectName: "notifications"
            target: input
        }
    }
    TextInput {
        objectName: "other"
        y: 70
        width: 320
        height: 30
        readOnly: true
    }
}
)").arg(input_type).toUtf8(), QUrl(QStringLiteral("qrc:/tests/clipboard_input.qml")));
        m_root.reset(component.create());
        m_error = component.errorString();
        if (auto* item = qobject_cast<QQuickItem*>(m_root.get())) {
            item->setParentItem(m_window->contentItem());
        }
    }

    QQuickItem* input() const { return m_root->findChild<QQuickItem*>(QStringLiteral("input")); }
    QQuickItem* other() const { return m_root->findChild<QQuickItem*>(QStringLiteral("other")); }
    QObject* notifications() const { return m_root->findChild<QObject*>(QStringLiteral("notifications")); }

    void show()
    {
        if (!m_render_control) {
            m_window->show();
        }
    }

    // The scene dies before its window, and the render control outlives both.
    std::unique_ptr<QQuickRenderControl> m_render_control;
    std::unique_ptr<QQuickWindow> m_window;
    QQmlEngine m_engine;
    std::unique_ptr<QObject> m_root;
    QString m_error;
};

QVariantMap editing_state(QObject* input)
{
    QVariantMap state;
    for (const auto* name : {"text", "cursorPosition", "selectionStart", "selectionEnd", "canUndo", "canRedo", "readOnly"}) {
        state.insert(QString::fromLatin1(name), input->property(name));
    }
    return state;
}

std::shared_ptr<Clipboard_observation> observe_clipboard()
{
    auto observation = std::make_shared<Clipboard_observation>();
    QGuiApplication::clipboard()->setMimeData(new Observed_clipboard_data(observation));
    observation->probes = 0;
    return observation;
}

} // namespace

class VNM_Chrome_text_input_clipboard_tests : public QObject
{
    Q_OBJECT

private slots:
    void cleanup()
    {
        QGuiApplication::clipboard()->clear();
    }

    void notifications_follow_presentation_data()
    {
        QTest::addColumn<QString>("input_type");
        QTest::addColumn<bool>("offscreen");
        for (const auto* type : {"TextInput", "TextEdit", "TextField", "TextArea"}) {
            QTest::newRow(qPrintable(QString::fromLatin1(type) + QStringLiteral("-native")))
                << QString::fromLatin1(type) << false;
            QTest::newRow(qPrintable(QString::fromLatin1(type) + QStringLiteral("-render-control")))
                << QString::fromLatin1(type) << true;
        }
    }

    void notifications_follow_presentation()
    {
        QFETCH(QString, input_type);
        QFETCH(bool, offscreen);
        Input_scene scene(offscreen, input_type);
        QVERIFY2(scene.m_root != nullptr, qPrintable(scene.m_error));
        auto* const input = scene.input();
        QVERIFY(input);
        QVERIFY(scene.notifications()->property("supported").toBool());
        const auto clipboard = observe_clipboard();
        scene.show();
        clipboard->probes = 0;
        QGuiApplication::clipboard()->dataChanged();
        const int active_probes = clipboard->probes;
        QVERIFY(active_probes > 0);
        if (offscreen) {
            QVERIFY(!scene.m_window->isVisible());
        }

        for (int cycle = 0; cycle < 3; ++cycle) {
            input->setVisible(false);
            clipboard->probes = 0;
            QSignalSpy other_subscriber(QGuiApplication::clipboard(), &QClipboard::dataChanged);
            QGuiApplication::clipboard()->dataChanged();
            QCOMPARE(clipboard->probes, 0);
            QCOMPARE(other_subscriber.count(), 1);
            input->setVisible(true);
            clipboard->probes = 0;
            QGuiApplication::clipboard()->dataChanged();
            QCOMPARE(clipboard->probes, active_probes);
        }

        input->setEnabled(false);
        clipboard->probes = 0;
        QGuiApplication::clipboard()->dataChanged();
        QCOMPARE(clipboard->probes, 0);
        input->setEnabled(true);
        clipboard->probes = 0;
        QGuiApplication::clipboard()->dataChanged();
        QCOMPARE(clipboard->probes, active_probes);

        const auto before_presentation = editing_state(input);
        QVERIFY(scene.notifications()->setProperty("presentation_active", false));
        clipboard->probes = 0;
        QGuiApplication::clipboard()->dataChanged();
        QCOMPARE(clipboard->probes, 0);
        QVERIFY(input->isVisible());
        QVERIFY(scene.notifications()->setProperty("presentation_active", true));
        QCOMPARE(editing_state(input), before_presentation);
        clipboard->probes = 0;
        QGuiApplication::clipboard()->dataChanged();
        QCOMPARE(clipboard->probes, active_probes);

        auto* const root = qobject_cast<QQuickItem*>(scene.m_root.get());
        root->setVisible(false);
        clipboard->probes = 0;
        QGuiApplication::clipboard()->dataChanged();
        QCOMPARE(clipboard->probes, 0);
        root->setVisible(true);
        if (!offscreen) {
            scene.m_window->hide();
            clipboard->probes = 0;
            QGuiApplication::clipboard()->dataChanged();
            QCOMPARE(clipboard->probes, 0);
            scene.m_window->show();
        }
        root->setParentItem(nullptr);
        clipboard->probes = 0;
        QGuiApplication::clipboard()->dataChanged();
        QCOMPARE(clipboard->probes, 0);
    }

    void editing_and_bindings_survive_suspension_data()
    {
        QTest::addColumn<QString>("input_type");
        for (const auto* type : {"TextInput", "TextEdit", "TextField", "TextArea"}) {
            QTest::newRow(type) << QString::fromLatin1(type);
        }
    }

    void popup_inputs_follow_their_presentation_data()
    {
        QTest::addColumn<QString>("popup_type");
        QTest::newRow("item") << QStringLiteral("Item");
        QTest::newRow("window") << QStringLiteral("Window");
    }

    void popup_inputs_follow_their_presentation()
    {
        QFETCH(QString, popup_type);
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));
        QQmlComponent component(&engine);
        component.setData(QStringLiteral(R"(
import QtQuick
import QtQuick.Controls.Basic
import VNM_Chrome

ApplicationWindow {
    width: 360
    height: 180

    Popup {
        objectName: "form"
        popupType: Popup.%1
        width: 240
        height: 90
        enter: null
        exit: null

        TextField {
            id: input
            objectName: "popup_input"
            width: 200
            text: "Popup input"

            VNM_TextInputClipboardNotifications {
                objectName: "popup_notifications"
                target: input
            }
        }
    }
}
)").arg(popup_type).toUtf8(), QUrl(QStringLiteral("qrc:/tests/clipboard_popup.qml")));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root != nullptr, qPrintable(component.errorString()));
        auto* const window = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(window);
        window->show();
        auto* const form = root->findChild<QObject*>(QStringLiteral("form"));
        QVERIFY(form);
        const auto clipboard = observe_clipboard();
        QVERIFY(QMetaObject::invokeMethod(form, "open"));
        QTRY_VERIFY(form->property("opened").toBool());
        auto* const input = root->findChild<QQuickItem*>(QStringLiteral("popup_input"));
        auto* const notifications = root->findChild<QObject*>(QStringLiteral("popup_notifications"));
        QVERIFY(input && notifications);
        QVERIFY(notifications->property("supported").toBool());
        clipboard->probes = 0;
        QGuiApplication::clipboard()->dataChanged();
        const int active_probes = clipboard->probes;
        QVERIFY(active_probes > 0);
        QVERIFY(QMetaObject::invokeMethod(form, "close"));
        clipboard->probes = 0;
        QGuiApplication::clipboard()->dataChanged();
        QCOMPARE(clipboard->probes, 0);
        clipboard->has_text = false;
        QVERIFY(QMetaObject::invokeMethod(form, "open"));
        QTRY_VERIFY(form->property("opened").toBool());
        QVERIFY(!input->property("canPaste").toBool());
        clipboard->has_text = true;
        clipboard->probes = 0;
        QGuiApplication::clipboard()->dataChanged();
        QCOMPARE(clipboard->probes, active_probes);
    }

    void editing_and_bindings_survive_suspension()
    {
        QFETCH(QString, input_type);
        Input_scene scene(true, input_type);
        QVERIFY2(scene.m_root != nullptr, qPrintable(scene.m_error));
        auto* const input = scene.input();
        const auto clipboard = observe_clipboard();
        QVERIFY(QMetaObject::invokeMethod(input, "insert", Q_ARG(int, 0), Q_ARG(QString, QStringLiteral("prefix "))));
        QVERIFY(QMetaObject::invokeMethod(input, "select", Q_ARG(int, 3), Q_ARG(int, 8)));
        const auto before = editing_state(input);
        QVERIFY(input->property("canUndo").toBool());
        input->setVisible(false);
        input->setEnabled(false);
        input->setEnabled(true);
        input->setVisible(true);
        QCOMPARE(editing_state(input), before);

        scene.m_root->setProperty("input_read_only", true);
        QVERIFY(input->property("readOnly").toBool());
        input->setVisible(false);
        scene.m_root->setProperty("input_read_only", false);
        input->setVisible(true);
        QVERIFY(!input->property("readOnly").toBool());
        QVERIFY(input->property("canPaste").toBool());
        input->setVisible(false);
        clipboard->has_text = false;
        clipboard->probes = 0;
        QGuiApplication::clipboard()->dataChanged();
        QCOMPARE(clipboard->probes, 0);
        input->setVisible(true);
        QVERIFY(!input->property("canPaste").toBool());
        clipboard->has_text = true;
        QGuiApplication::clipboard()->dataChanged();
        QVERIFY(input->property("canPaste").toBool());

        input->setProperty("text", QString{});
        input->forceActiveFocus(Qt::OtherFocusReason);
        QKeyEvent key(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, QStringLiteral("x"));
        QCoreApplication::sendEvent(scene.m_window.get(), &key);
        QCOMPARE(input->property("text").toString(), QStringLiteral("x"));
        QInputMethodEvent commit;
        commit.setCommitString(QStringLiteral("\u03bb"));
        QCoreApplication::sendEvent(input, &commit);
        QCOMPARE(input->property("text").toString(), QStringLiteral("x\u03bb"));
        QVERIFY(QMetaObject::invokeMethod(input, "paste"));
        QCOMPARE(input->property("text").toString(), QStringLiteral("x\u03bbclipboard text"));
    }

    void reentrant_refresh_preserves_ownership_data()
    {
        QTest::addColumn<QString>("action");
        for (const auto* action : {"hide", "disable", "retarget", "delete-helper", "delete-target"}) {
            QTest::newRow(action) << QString::fromLatin1(action);
        }
    }

    void hand_back_refreshes_a_suspended_cache_data()
    {
        QTest::addColumn<QString>("input_type");
        QTest::addColumn<bool>("delete_helper");
        for (const auto* type : {"TextInput", "TextEdit", "TextField", "TextArea"}) {
            QTest::newRow(qPrintable(QString::fromLatin1(type) + QStringLiteral("-delete")))
                << QString::fromLatin1(type) << true;
            QTest::newRow(qPrintable(QString::fromLatin1(type) + QStringLiteral("-retarget")))
                << QString::fromLatin1(type) << false;
        }
    }

    void hand_back_refreshes_a_suspended_cache()
    {
        QFETCH(QString, input_type);
        QFETCH(bool, delete_helper);
        Input_scene scene(true, input_type);
        QVERIFY2(scene.m_root != nullptr, qPrintable(scene.m_error));
        auto* const input = scene.input();
        const auto clipboard = observe_clipboard();
        QVERIFY(input->property("canPaste").toBool());
        input->setVisible(false);
        clipboard->has_text = false;
        clipboard->probes = 0;
        QGuiApplication::clipboard()->dataChanged();
        QCOMPARE(clipboard->probes, 0);
        if (delete_helper) {
            delete scene.notifications();
        }
        else {
            QVERIFY(scene.notifications()->setProperty("target", QVariant::fromValue(scene.other())));
        }
        QVERIFY(!input->property("canPaste").toBool());
        input->setVisible(true);
        QVERIFY(!input->property("canPaste").toBool());
        clipboard->has_text = true;
        QGuiApplication::clipboard()->dataChanged();
        QVERIFY(input->property("canPaste").toBool());
    }

    void reentrant_hand_back_preserves_the_later_target_data()
    {
        QTest::addColumn<QString>("action");
        QTest::newRow("delete") << QStringLiteral("delete");
        QTest::newRow("third-target") << QStringLiteral("third-target");
        QTest::newRow("original-target") << QStringLiteral("original-target");
    }

    void reentrant_hand_back_preserves_the_later_target()
    {
        QFETCH(QString, action);
        Input_scene scene(true);
        QVERIFY2(scene.m_root != nullptr, qPrintable(scene.m_error));
        const QPointer<QObject> notifications = scene.notifications();
        const auto clipboard = observe_clipboard();
        QVERIFY(scene.input()->property("canPaste").toBool());
        scene.input()->setVisible(false);
        clipboard->has_text = false;
        QGuiApplication::clipboard()->dataChanged();
        clipboard->on_probe = [&] {
            if (action == QStringLiteral("delete")) {
                delete notifications.data();
            }
            else {
                auto* const target = action == QStringLiteral("original-target")
                    ? scene.input() : qobject_cast<QQuickItem*>(scene.m_root.get());
                notifications->setProperty("target", QVariant::fromValue(target));
            }
        };
        notifications->setProperty("target", QVariant::fromValue(scene.other()));
        QVERIFY(!clipboard->on_probe);
        if (action == QStringLiteral("delete")) {
            QVERIFY(notifications.isNull());
        }
        else {
            auto* const target = action == QStringLiteral("original-target")
                ? scene.input() : qobject_cast<QQuickItem*>(scene.m_root.get());
            QCOMPARE(notifications->property("target").value<QQuickItem*>(), target);
            QCOMPARE(notifications->property("supported").toBool(), action == QStringLiteral("original-target"));
        }
        QVERIFY(!scene.input()->property("canPaste").toBool());
        if (action != QStringLiteral("original-target")) {
            return;
        }
        // Reacquiring the old pointer must restore the complete guard. The
        // hidden input stays suspended and refreshes once when shown again.
        clipboard->has_text = true;
        clipboard->probes = 0;
        QGuiApplication::clipboard()->dataChanged();
        QCOMPARE(clipboard->probes, 0);
        scene.input()->setVisible(true);
        QVERIFY(scene.input()->property("canPaste").toBool());
        const int refresh_probes = clipboard->probes;
        QVERIFY(refresh_probes > 0);
        clipboard->probes = 0;
        QGuiApplication::clipboard()->dataChanged();
        QCOMPARE(clipboard->probes, refresh_probes);
        scene.input()->setVisible(false);
        clipboard->probes = 0;
        QGuiApplication::clipboard()->dataChanged();
        QCOMPARE(clipboard->probes, 0);
    }

    void reentrant_refresh_preserves_ownership()
    {
        QFETCH(QString, action);
        Input_scene scene(true);
        QVERIFY2(scene.m_root != nullptr, qPrintable(scene.m_error));
        const QPointer<QQuickItem> input = scene.input();
        const QPointer<QObject> notifications = scene.notifications();
        const auto clipboard = observe_clipboard();
        input->setVisible(false);
        clipboard->on_probe = [&] {
            if (action == QStringLiteral("hide")) {
                input->setVisible(false);
            }
            else
            if (action == QStringLiteral("disable")) {
                input->setEnabled(false);
            }
            else
            if (action == QStringLiteral("retarget")) {
                scene.other()->setProperty("readOnly", false);
                notifications->setProperty("target", QVariant::fromValue(scene.other()));
            }
            else
            if (action == QStringLiteral("delete-helper")) {
                delete notifications.data();
            }
            else {
                // Qt owns the executing input slot. Its normal deferred
                // destruction lets that slot finish before deleting the item.
                input->deleteLater();
            }
        };
        input->setVisible(true);
        QVERIFY(!clipboard->on_probe);
        if (action == QStringLiteral("delete-target")) {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QVERIFY(input.isNull());
            QVERIFY(notifications.isNull());
        }
        else
        if (action == QStringLiteral("retarget")) {
            QCOMPARE(notifications->property("target").value<QQuickItem*>(), scene.other());
            QVERIFY(notifications->property("supported").toBool());
            input->setProperty("readOnly", true);
            scene.other()->setVisible(false);
        }
        else
        if (action == QStringLiteral("delete-helper")) {
            QVERIFY(notifications.isNull());
            clipboard->probes = 0;
            QGuiApplication::clipboard()->dataChanged();
            QVERIFY(clipboard->probes > 0);
            return;
        }
        clipboard->probes = 0;
        QGuiApplication::clipboard()->dataChanged();
        QCOMPARE(clipboard->probes, 0);
    }

    void unsupported_target_is_reported()
    {
        Input_scene scene(true);
        QVERIFY2(scene.m_root != nullptr, qPrintable(scene.m_error));
        QVERIFY(scene.notifications()->setProperty("target", QVariant::fromValue(qobject_cast<QQuickItem*>(scene.m_root.get()))));
        QVERIFY(!scene.notifications()->property("supported").toBool());
        const auto clipboard = observe_clipboard();
        clipboard->probes = 0;
        QGuiApplication::clipboard()->dataChanged();
        QVERIFY(clipboard->probes > 0);
    }
};

QTEST_MAIN(VNM_Chrome_text_input_clipboard_tests)

#include "vnm_text_input_clipboard_tests.moc"
