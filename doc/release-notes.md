OpenSCAD nightly 2026.09.01 with OpenAxis navigation.

Connects automatically to Rotatrix and supports native orbit navigation, surface
picking and viewport diagnostics. Includes repairs for navigation in an empty
viewport and reconnecting after starting while the device is unavailable.

Downloads:
- Windows x86_64: extract the Qt6 ZIP; run openscad.exe. Packages are unsigned.
- macOS Apple Silicon / Intel: extract the signed, notarized app archive and move OpenSCAD.app to Applications.
- Linux x86_64: AppImage built with upstream's Ubuntu 22.04 recipe. Mark it executable and run it; Linux ARM64 is not a release target.

Preview limitations:
- macOS software rendering: previews may show visual artifacts when using Apple's software renderer. This also occurs upstream; it was not reproduced with hardware rendering on the tested M1 Pro.
- SVG arcs on Apple Silicon: some imported arcs may have slightly different segmentation due to an upstream rounding issue.

This preview shares OpenSCAD's application identity and preferences with upstream.
Source and pinned dependency references are available at this release's immutable
Git tag; see doc/rotatrix.md for usage/build information. SHA256SUMS covers all downloads.
