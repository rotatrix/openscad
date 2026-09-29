# Rotatrix / OpenAxis fork

Baseline: official `openscad-2021.01` release. Work branch:
`rotatrix/work/openscad-2021.01`; maintained branch (after validation):
`rotatrix/openscad-2021.01`. Published maintained history is append-only.

OpenAxis SDK: `cpp/v1.0.0-rc.1`, pinned to
`acc4da095cde6747556245b4b6c110c16b968b6b`.

Enable with CMake `-DENABLE_OPENAXIS=ON`, or for qmake:

```
cmake -S cmake/openaxis-qmake -B openaxis-build -DCMAKE_BUILD_TYPE=Release
cmake --build openaxis-build --parallel 2
qmake openscad.pro CONFIG+=openaxis
```

Navigation uses the native gimbal camera, including perspective/orthographic
projection. Each viewport owns its SDK session and Qt scheduler. Focus loss,
scene replacement and viewport changes cancel in-flight navigation. Geometry
selection facts are unavailable because OpenSCAD has no persistent selection.
Ctrl+Shift+D toggles the compact diagnostics display.

## Validation still required

Compile and run the upstream tests on Linux, Windows and macOS; verify usable
packages. Exercise native mouse/device alternation, scene recompilation,
multiple windows, modal dialogs, high DPI, cursor and center picking, projection
switching and shutdown with queued callbacks. Do not promote this work branch or
publish a release until these checks pass.

## CI compatibility

The baseline uses Ubuntu 18.04/20.04, moving Windows/macOS labels and artifact
upload v2. Adaptations use explicit hosted images and artifact upload v4. Linux
uses distribution dependencies instead of the obsolete external lib3mf apt
repository. Upstream experimental options and test exclusions are preserved.
The SDK requires CMake >=3.24 and C++17; upstream builds without the feature
retain their original language setting. No upstream-main workflow was copied.

Final tags follow `openscad-2021.01-rotatrix.N`; routine testing uses artifacts.
Release packaging/signing and maintained-branch promotion remain pending until
platform validation completes.
