#include "vnm_qml_chrome/vnm_qml_chrome_runtime.h"

#include "vnm_qml_chrome/vnm_chrome_geometry.h"
#include "vnm_qml_chrome/vnm_native_window_frame.h"
#include "vnm_qml_chrome/vnm_system_window.h"

#include <QDebug>
#include <QJSEngine>
#include <QQmlEngine>
#include <QResource>
#include <QString>
#include <qqml.h>

#include <mutex>
#include <string>

// Forward-declare the generated resource initializer so that the linker pulls
// in vnm_qml_chrome.qrc when this target is consumed as a static library.
extern int qInitResources_vnm_qml_chrome();
extern int qInitResources_vnm_qml_chrome_icon_shaders();

namespace {

constexpr const char* k_chrome_qmldir_resource = ":/vnm_qml_chrome/qml/VNM_Chrome/qmldir";
constexpr const char* k_chrome_qml_import_path = "qrc:/vnm_qml_chrome/qml";

std::once_flag s_qrc_init_flag;
std::once_flag s_qml_type_registration_flag;

void ensure_qrc_initialized()
{
    std::call_once(s_qrc_init_flag, [] {
        qInitResources_vnm_qml_chrome();
        qInitResources_vnm_qml_chrome_icon_shaders();
    });
}

void ensure_qml_types_registered()
{
    std::call_once(s_qml_type_registration_flag, [] {
        qmlRegisterSingletonType<vnm_qml_chrome::Chrome_geometry>(
            "VNM_Chrome",
            1,
            0,
            "VNM_chrome_geometry",
            [](QQmlEngine*, QJSEngine*) -> QObject* {
                return new vnm_qml_chrome::Chrome_geometry;
            });
        qmlRegisterSingletonType<vnm_qml_chrome::System_window>(
            "VNM_Chrome",
            1,
            0,
            "VNM_system_window",
            [](QQmlEngine*, QJSEngine*) -> QObject* {
                return new vnm_qml_chrome::System_window;
            });
        qmlRegisterType<VNM_NativeWindowFrame>(
            "VNM_Chrome",
            1,
            0,
            "VNM_NativeWindowFrame");
    });
}

bool verify_chrome_resources_available()
{
    const QResource qmldir_resource(QString::fromUtf8(k_chrome_qmldir_resource));
    if (qmldir_resource.isValid()) {
        return true;
    }

    const std::string detail =
        std::string("vnm_qml_chrome resources are unavailable. ")
        + "The automatic Q_INIT_RESOURCE failed -- this should not happen. "
        + "Missing resource: " + k_chrome_qmldir_resource;
    qCritical().noquote() << QStringLiteral("FATAL: %1").arg(QString::fromStdString(detail));
    return false;
}

} // namespace

bool vnm_init_qml_chrome_runtime(QQmlEngine& engine)
{
    ensure_qrc_initialized();
    ensure_qml_types_registered();

    if (!verify_chrome_resources_available()) {
        return false;
    }

    const QString import_path = QString::fromUtf8(k_chrome_qml_import_path);
    if (!engine.importPathList().contains(import_path)) {
        engine.addImportPath(import_path);
    }
    return true;
}
