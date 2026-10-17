# OpenAxis navigation

Build with `-DENABLE_OPENAXIS=ON` to enable OpenAxis navigation in the GUI.
CMake 3.24 or newer is required. The SDK is pinned in `cmake/OpenAxis.cmake`;
`-DOPENAXIS_SOURCE_DIR=/path/to/openaxis` selects a local SDK checkout.
Use the upstream platform build instructions and dependency scripts.

OpenSCAD connects automatically to Rotatrix. Use **View > OpenAxis Diagnostics**
to display connection status, navigation facts, picks and pivots in the viewport.
Navigation uses the native orbit camera; mouse navigation remains available.
Free-camera and object manipulation modes are not provided. Surface picking works
in preview and render modes; per-body bounds are available in CSG preview modes.
OpenSCAD has no persistent geometry selection, so selection-only queries are unavailable.

Packages preserve OpenSCAD's application identity and preferences. An upstream and
Rotatrix build share settings and can replace one another in Applications.
Windows downloads are portable ZIPs: extract the complete archive and run
`openscad.exe` (`openscad.com` for command-line use). The Linux package targets
Ubuntu 24.04 x86_64. On macOS, extract the archive and move `OpenSCAD.app` to Applications.
Windows packages are unsigned; macOS release packages are signed and notarized.

Application source is available at the release tag. OpenAxis and its dependencies
are pinned by `cmake/OpenAxis.cmake` and the SDK's CMake files. Upstream dependencies
and patches are recorded in the submodules and platform dependency scripts.
Required OpenAxis/IXWebSocket notices accompany installed resources in `licenses`.
See each release's notes for testing limitations.
