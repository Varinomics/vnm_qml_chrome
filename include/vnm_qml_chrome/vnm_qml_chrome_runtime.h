#pragma once

class QQmlEngine;

/// Initialize VNM_Chrome QML resources and configure engine import paths.
/// Must be called once per engine before importing VNM_Chrome.
/// The qrc initialization is internally guarded and safe to call from
/// multiple engines.
/// Also registers the VNM_chrome_geometry and VNM_system_window singletons and
/// native frame and VNM_TextInputFocusController types.
/// Returns false only on fatal resource-verification failure.
bool vnm_init_qml_chrome_runtime(QQmlEngine& engine);
