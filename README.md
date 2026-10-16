# vnm_qml_chrome

[![CI Linux](https://github.com/Varinomics/vnm_qml_chrome/actions/workflows/ci-linux.yml/badge.svg?branch=master)](https://github.com/Varinomics/vnm_qml_chrome/actions/workflows/ci-linux.yml) [![CI macOS](https://github.com/Varinomics/vnm_qml_chrome/actions/workflows/ci-macos.yml/badge.svg?branch=master)](https://github.com/Varinomics/vnm_qml_chrome/actions/workflows/ci-macos.yml) [![CI Windows](https://github.com/Varinomics/vnm_qml_chrome/actions/workflows/ci-windows.yml/badge.svg?branch=master)](https://github.com/Varinomics/vnm_qml_chrome/actions/workflows/ci-windows.yml) [![CI FreeBSD](https://github.com/Varinomics/vnm_qml_chrome/actions/workflows/ci-freebsd.yml/badge.svg?branch=master)](https://github.com/Varinomics/vnm_qml_chrome/actions/workflows/ci-freebsd.yml)

Shared Qt Quick window chrome for Varinomics applications: title bars, window
frames, resize controls, the animated Varinomics mark and device-pixel-aligned
controls. It requires C++20 and Qt 6.12 or newer.

## Integrate

Add the source project and link its target so Qt retains the compiled QML units
and resource initializers:

```cmake
add_subdirectory(path/to/vnm_qml_chrome)
target_link_libraries(my_app PRIVATE vnm_qml_chrome::vnm_qml_chrome)
```

After creating the host's `QGuiApplication`, initialize every engine before it
loads QML that imports `VNM_Chrome`:

```cpp
#include "vnm_qml_chrome/vnm_qml_chrome_runtime.h"
#include <QQmlApplicationEngine>

QQmlApplicationEngine engine;
if (!vnm_init_qml_chrome_runtime(engine)) {
    return 1;
}
```

The initializer registers resources, import paths and singletons. A false return
means resource verification failed. The complete
[solid-window example](examples/solid_window) shows `VNM_ChromeFrameShell`,
native move/resize requests and the owning window's actions together.

Rebuild the library and consumers when changing Qt: embedded compiled QML units
require the matching Qt build at runtime. Hosts choose their own QML disk-cache
policy before creating the application or engine; see
[compiled QML and runtime policy](docs/runtime.md).

## Reference and licensing

- [Controls and window integration](docs/controls.md): fonts, title editing,
  topmost state, native foreground coordination and LCD icon composition.
- [Compiled QML and runtime policy](docs/runtime.md).
- [Third-party notices](THIRD_PARTY_NOTICES.md).
