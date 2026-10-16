# Compiled QML and host runtime policy

The module compiles its QML with `qt_add_qml_module(QML_FILES ...)` and
discards source contents in Release builds. Other configurations retain source.
Keep `QT_QML_NO_CACHEGEN=OFF` (the default); configure
rejects disabling generation. Qt compiles supported bindings to native code
and retains compiled bytecode for the others. The embedded compiled units
require the matching Qt
build at runtime. Rebuild the library and its consumers when changing Qt.
Release source text is unavailable for fallback or QML debugging that disables
compiled units. Strings, metadata and compiled code remain
inspectable; this is not encryption.

Hosts that require no generated QML disk cache should set
`QML_DISK_CACHE=aot` and remove `QML_FORCE_DISK_CACHE` and
`QML_DISABLE_DISK_CACHE` before constructing any Qt application or QML engine.
Preserve the caller's environment separately for child processes. This library
does not change the host process's cache policy. Link the CMake target
`vnm_qml_chrome::vnm_qml_chrome` so Qt's compiled units and resource initializers
are retained.
