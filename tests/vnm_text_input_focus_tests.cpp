#include "vnm_qml_chrome/vnm_qml_chrome_runtime.h"

#include <QCoreApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QUrl>
#include <QVariant>
#include <QtTest/QTest>

#include <memory>

class VNM_Chrome_text_input_focus_tests : public QObject
{
    Q_OBJECT

private slots:
    void scoped_editors_and_controls_keep_independent_focus()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import VNM_Chrome

Window {
    id: gallery_window
    visible: true
    width: 700
    height: 500

    VNM_TextInputFocusController {
        objectName: "controller"
        window: gallery_window
        scope: proposed_scope
    }

    Item {
        id: proposed_scope
        objectName: "scope"
        x: 20
        y: 20
        width: 300
        height: 400

        TextInput {
            objectName: "raw_input"
            width: 260
            height: 32
            text: "raw input"
        }
        TextEdit {
            objectName: "raw_edit"
            y: 40
            width: 260
            height: 50
            readOnly: true
            text: "read-only output"
        }
        Button {
            objectName: "button"
            y: 100
            width: 120
            text: "Apply"
        }
        CheckBox {
            objectName: "check"
            y: 150
            checked: true
            text: "Option"
        }
        TextField {
            id: field
            objectName: "field"
            y: 200
            width: 260
            text: "popup owner"
        }
        ComboBox {
            objectName: "combo"
            y: 250
            width: 260
            editable: true
            model: ["first", "second"]
        }
        SpinBox {
            id: spin
            objectName: "spin"
            y: 300
            width: 160
            editable: true
            value: 14
            contentItem: TextInput {
                text: spin.displayText
                validator: spin.validator
            }
        }
        Popup {
            objectName: "popup"
            parent: field
            y: field.height + 4
            width: 240
            height: 100
            focus: true
            closePolicy: Popup.CloseOnEscape

            TextField {
                objectName: "popup_field"
                width: 220
                text: "popup editor"
            }
        }
    }

    TextInput {
        objectName: "outside_input"
        x: 360
        y: 20
        width: 260
        height: 32
        text: "comparison input"
    }
}
)";

        QQmlComponent component(&engine);
        component.setData(qml_source, QUrl(QStringLiteral("qrc:/tests/focus_scope.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root != nullptr, qPrintable(component.errorString()));
        auto* const window = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(window != nullptr);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));

        QObject* const controller = root->findChild<QObject*>(QStringLiteral("controller"));
        auto* const scope = root->findChild<QQuickItem*>(QStringLiteral("scope"));
        auto* const outside_input = root->findChild<QQuickItem*>(QStringLiteral("outside_input"));
        auto* const button = root->findChild<QQuickItem*>(QStringLiteral("button"));
        auto* const check = root->findChild<QQuickItem*>(QStringLiteral("check"));
        auto* const field = root->findChild<QQuickItem*>(QStringLiteral("field"));
        QObject* const popup = root->findChild<QObject*>(QStringLiteral("popup"));
        auto* const popup_field = root->findChild<QQuickItem*>(QStringLiteral("popup_field"));
        QVERIFY(controller && scope && outside_input && button && check && field && popup && popup_field);
        QCOMPARE(controller->property("window").value<QQuickWindow*>(), window);
        const auto click_blank = [window] {
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(670, 470));
        };

        for (const char* name : {"raw_input", "raw_edit"}) {
            auto* const editor = root->findChild<QQuickItem*>(QString::fromLatin1(name));
            QVERIFY(editor != nullptr);
            editor->forceActiveFocus(Qt::TabFocusReason);
            QVERIFY(editor->hasActiveFocus());
            click_blank();
            QTRY_VERIFY(!editor->hasFocus());
        }

        outside_input->forceActiveFocus(Qt::TabFocusReason);
        click_blank();
        QVERIFY(outside_input->hasActiveFocus());

        button->forceActiveFocus(Qt::TabFocusReason);
        click_blank();
        QVERIFY(button->hasActiveFocus());
        QVERIFY(controller->setProperty("include_controls", true));
        click_blank();
        QTRY_VERIFY(!button->hasFocus());

        check->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_Space);
        QVERIFY(!check->property("checked").toBool());
        click_blank();
        QTRY_VERIFY(!check->hasFocus());
        QVERIFY(!check->property("checked").toBool());

        for (const char* name : {"combo", "spin"}) {
            auto* const control = root->findChild<QQuickItem*>(QString::fromLatin1(name));
            QVERIFY(control != nullptr);
            auto* const editor = control->property("contentItem").value<QQuickItem*>();
            QVERIFY(editor != nullptr);
            const QString text = editor->property("text").toString();
            editor->forceActiveFocus(Qt::TabFocusReason);
            QVERIFY(editor->hasActiveFocus());
            QVERIFY(control->hasActiveFocus());
            click_blank();
            QTRY_VERIFY(!control->hasFocus());
            QVERIFY(!editor->hasFocus());
            QCOMPARE(editor->property("text").toString(), text);
        }

        QSignalSpy clicked(button, SIGNAL(clicked()));
        QVERIFY(clicked.isValid());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
        QCOMPARE(clicked.count(), 1);

        field->forceActiveFocus(Qt::TabFocusReason);
        QVERIFY(QMetaObject::invokeMethod(popup, "open"));
        QTRY_VERIFY(popup->property("opened").toBool());
        popup_field->forceActiveFocus(Qt::TabFocusReason);
        QVERIFY(popup_field->hasActiveFocus());
        click_blank();
        QTRY_VERIFY(!popup_field->hasFocus());
        QVERIFY(popup->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(popup, "close"));
        QTRY_VERIFY(!popup->property("visible").toBool());
        QVERIFY(!field->hasFocus());

        // Removing the optional scope deliberately restores ordinary window-wide
        // editor behavior; controls are still opt-in.
        QVERIFY(controller->setProperty("scope", QVariant::fromValue<QQuickItem*>(nullptr)));
        QVERIFY(controller->setProperty("include_controls", false));
        outside_input->forceActiveFocus(Qt::TabFocusReason);
        click_blank();
        QTRY_VERIFY(!outside_input->hasFocus());
        button->forceActiveFocus(Qt::TabFocusReason);
        click_blank();
        QVERIFY(button->hasActiveFocus());

        QVERIFY(controller->setProperty("scope", QVariant::fromValue(scope)));
        QVERIFY(controller->setProperty("include_controls", true));
        auto* const spin = root->findChild<QQuickItem*>(QStringLiteral("spin"));
        auto* const spin_editor = spin->property("contentItem").value<QQuickItem*>();
        spin_editor->forceActiveFocus(Qt::TabFocusReason);
        window->hide();
        QVERIFY(!spin->hasFocus());
        QVERIFY(!spin_editor->hasFocus());
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        spin_editor->forceActiveFocus(Qt::TabFocusReason);
        delete scope;
        QCOMPARE(controller->property("window").value<QQuickWindow*>(), nullptr);
    }

    void independent_popup_control_clears_focus_on_blank_press()
    {
        QQmlEngine engine;
        QVERIFY(vnm_init_qml_chrome_runtime(engine));

        static const char qml_source[] = R"(
import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import VNM_Chrome

Window {
    id: window
    visible: true
    width: 500
    height: 360

    VNM_TextInputFocusController {
        window: window
        scope: opener
        include_controls: true
    }
    Button {
        id: opener
        objectName: "opener"
        x: 30
        y: 30
        text: "Appearance"
        focusPolicy: Qt.NoFocus
    }
    Popup {
        objectName: "form"
        parent: opener
        y: opener.height
        width: 300
        height: 180
        popupType: Popup.Item
        closePolicy: Popup.CloseOnEscape

        contentItem: ScrollView {
            contentWidth: availableWidth
            contentHeight: 140

            ComboBox {
                id: combo
                objectName: "combo"
                width: 260
                focusPolicy: Qt.TabFocus
                model: ["Mono", "System"]
                contentItem: Text {
                    text: combo.displayText
                }
                delegate: ItemDelegate {
                    required property int index
                    required property var modelData
                    objectName: "choice_" + index
                    width: combo.width
                    text: modelData
                }
            }
            Label {
                objectName: "blank"
                y: 80
                width: 260
                height: 30
                text: "Font"
            }
        }
    }
}
)";

        QQmlComponent component(&engine);
        component.setData(qml_source, QUrl(QStringLiteral("qrc:/tests/independent_popup_control.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root != nullptr, qPrintable(component.errorString()));
        auto* const window = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(window != nullptr);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));

        QObject* const form = root->findChild<QObject*>(QStringLiteral("form"));
        auto* const opener = root->findChild<QQuickItem*>(QStringLiteral("opener"));
        auto* const combo = root->findChild<QQuickItem*>(QStringLiteral("combo"));
        auto* const blank = root->findChild<QQuickItem*>(QStringLiteral("blank"));
        QVERIFY(form && opener && combo && blank);
        opener->forceActiveFocus(Qt::OtherFocusReason);
        QVERIFY(QMetaObject::invokeMethod(form, "open"));
        QTRY_VERIFY(form->property("opened").toBool());
        combo->forceActiveFocus(Qt::TabFocusReason);
        QVERIFY(combo->hasActiveFocus());
        QVERIFY(!opener->hasFocus());

        const QPoint blank_position = blank->mapToScene(
            QPointF(blank->width() / 2, blank->height() / 2)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, blank_position);
        QTRY_VERIFY(!combo->hasFocus());
        QVERIFY(form->property("visible").toBool());

        QObject* const choices = combo->property("popup").value<QObject*>();
        QVERIFY(choices);
        const QPoint combo_position = combo->mapToScene(
            QPointF(combo->width() / 2, combo->height() / 2)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, combo_position);
        QTRY_VERIFY(choices->property("visible").toBool());
        QQuickItem* const choice_list = choices->property("contentItem").value<QQuickItem*>();
        QVERIFY(choice_list);
        QQuickItem* choice = nullptr;
        QTRY_VERIFY(QMetaObject::invokeMethod(choice_list, "itemAtIndex",
            Q_RETURN_ARG(QQuickItem*, choice), Q_ARG(int, 1)) && choice);
        const QPoint choice_position = choice->mapToScene(
            QPointF(choice->width() / 2, choice->height() / 2)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, choice_position);
        QTRY_COMPARE(combo->property("currentIndex").toInt(), 1);
        QTRY_VERIFY(!choices->property("visible").toBool());
        QVERIFY(form->property("visible").toBool());
    }
};

QTEST_MAIN(VNM_Chrome_text_input_focus_tests)

#include "vnm_text_input_focus_tests.moc"
