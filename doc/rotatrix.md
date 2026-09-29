# Rotatrix nightly OpenAxis integration

Upstream base: c2c321215e67b87d1f7f10f46f26bfafee04b215 (2026-09-01).
The user selected a passing nightly instead of the 2021.01 stable release.
All nine upstream push checks passed for this commit, including Linux release,
experimental and example tests, Windows Qt5/Qt6 and macOS Qt6.
Work branch: rotatrix/work/nightly-2026.09.01.
The previous rotatrix/work/openscad-2021.01 branch is abandoned and unmonitored.

Build with -DENABLE_OPENAXIS=ON. The feature is optional and requires a GUI.
SDK cpp/v1.0.0-rc.1 is pinned at acc4da095cde6747556245b4b6c110c16b968b6b.
Use -DOPENAXIS_SOURCE_DIR=/path/to/openaxis for local SDK development.
The View menu provides OpenAxis Navigation and OpenAxis Diagnostics toggles. Navigation shares the native camera and
invalidates gestures on scene replacement, focus loss or viewport changes.

The upstream nightly's platform matrices, dependencies and build commands are
preserved. Headless matrix entries keep the SDK disabled. Runner labels are
pinned where practical. Downloadable packages and interactive device validation
remain required before promotion to a maintained branch.

macOS CI reuses installed Homebrew dependencies and skips ccache because the
upstream build explicitly sets USE_CCACHE=OFF. This avoids rebuilding LLVM/Rust
for an unused build cache on Intel runners. Application features remain enabled.
