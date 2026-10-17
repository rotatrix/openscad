OpenSCAD nightly 2026.09.01 with OpenAxis navigation.

Connects automatically to Rotatrix and supports native orbit navigation, surface
picking and viewport diagnostics. Includes repairs for navigation in an empty
viewport and reconnecting after starting while the device is unavailable.

Downloads:
- Windows x86_64: extract the Qt6 ZIP; run openscad.exe. Packages are unsigned.
- macOS Apple Silicon / Intel: extract the signed, notarized app archive and move OpenSCAD.app to Applications.
- Linux x86_64: AppImage built with upstream's Ubuntu 22.04 recipe. Mark it executable and run it; Linux ARM64 is not a release target.

Preview limitations:
- ARM64 CI on Apple's software renderer exhibits intermittent corrupted preview/camera images, also reproduced with exact upstream packages. Repeated hardware-rendered tests on an M1 Pro passed; the underlying software-rendering fault is unresolved.
- Three ARM64 SVG arc comparisons reproduce upstream numerical rounding differences.
- macOS/Linux physical-device acceptance is incomplete. Automated package/startup checks do not establish device acceptance.

This preview shares OpenSCAD's application identity and preferences with upstream.
Source and pinned dependency references are available at this release's immutable
Git tag; see doc/rotatrix.md for usage/build information. SHA256SUMS covers all downloads.
