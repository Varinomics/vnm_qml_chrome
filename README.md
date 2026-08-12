# vnm_qml_chrome

[![CI Linux](https://github.com/imakris/vnm_qml_chrome/actions/workflows/ci-linux.yml/badge.svg?branch=master)](https://github.com/imakris/vnm_qml_chrome/actions/workflows/ci-linux.yml) [![CI macOS](https://github.com/imakris/vnm_qml_chrome/actions/workflows/ci-macos.yml/badge.svg?branch=master)](https://github.com/imakris/vnm_qml_chrome/actions/workflows/ci-macos.yml) [![CI Windows](https://github.com/imakris/vnm_qml_chrome/actions/workflows/ci-windows.yml/badge.svg?branch=master)](https://github.com/imakris/vnm_qml_chrome/actions/workflows/ci-windows.yml) [![CI FreeBSD](https://github.com/imakris/vnm_qml_chrome/actions/workflows/ci-freebsd.yml/badge.svg?branch=master)](https://github.com/imakris/vnm_qml_chrome/actions/workflows/ci-freebsd.yml)

Requires Qt 6.11.1 or newer.

`VNM_ChromeTitleBar` and `VNM_ChromeFrameShell` can opt into user-edited
window titles with `title_editing_enabled: true`. Alt+left-click starts editing;
Enter or moving focus away emits `title_edit_accepted(title)` so the owning
application can apply the accepted value. While editing, the available title
region is presented as a text box and the Varinomics mark stays in its Alt
pose. The capability is disabled by default.
