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

## Verified CI results (2026-09-29)

The application source is unchanged after e5c3de8cf41efcde5e2d90c85f52ab0421e06177;
subsequent commits repair only platform CI/package setup and documentation.

- Linux release build matrix (GUI/headless, tests on/off, Manifold on/off):
  https://github.com/rotatrix/openscad/actions/runs/36521060240
- Linux experimental tests (Ubuntu 22.04 Qt5, Ubuntu 24.04 Qt5/Qt6):
  https://github.com/rotatrix/openscad/actions/runs/36521060237
- Linux release tests: https://github.com/rotatrix/openscad/actions/runs/36521060209
- Linux example tests: https://github.com/rotatrix/openscad/actions/runs/36521060224
- Windows Qt6 passing job (the original Qt5 packaging failure was retried separately):
  https://github.com/rotatrix/openscad/actions/runs/36521060102/job/109253792226
- Windows Qt5 passing focused retry:
  https://github.com/rotatrix/openscad/actions/runs/36526929476
- macOS Intel Qt6 (2694 tests passed, including openaxis_camera):
  https://github.com/rotatrix/openscad/actions/runs/36529386196

Packages (10-day retention):
- Ubuntu 24.04 amd64 DEB: https://github.com/rotatrix/openscad/actions/runs/36521060237/artifacts/11013334404
- Windows Qt6: https://github.com/rotatrix/openscad/actions/runs/36521060102/artifacts/11013213825
- Windows Qt5: https://github.com/rotatrix/openscad/actions/runs/36526929476/artifacts/11015727395
- macOS Intel DMG: https://github.com/rotatrix/openscad/actions/runs/36529386196/artifacts/11017190355

Downloaded Windows Qt5 and Qt6 packages each exported a cube to STL successfully.
The Linux DEB was inspected for its executable, resources and runtime dependencies;
the macOS DMG download was checked for a valid UDIF trailer. Neither package was
launched locally on its target OS. Physical-device navigation and interactive GUI
acceptance remain unverified; these are test builds, not signed release claims.

## Apple Silicon coverage
The inherited upstream matrix excluded ARM64 due to a software-renderer concern.
Rotatrix now includes native macos-15 ARM64 alongside macos-15-intel x86_64.
Both run the same tests, with only the existing upstream PDF-font exclusion.
Packages must pass architecture checks for every Mach-O file, a native STL export,
and DMG integrity verification. ARM64 package verification passed in run 36613544426: every bundled Mach-O
supports ARM64, the packaged application exported STL natively, and the DMG
passed integrity verification. Download:
https://github.com/rotatrix/openscad/actions/runs/36613544426/artifacts/11053799626

Full test validation remains incomplete. The OpenAxis run failed 27 image tests.
An exact upstream source comparison (run 36617328767) also failed 27, with 26
shared failures. Only preview-cgal_highlight-modifier failed in the OpenAxis run;
only preview-manifold_minkowski3-erosion failed in the upstream run. This is not
sufficient evidence to declare zero regressions. Focused ARM64 reruns repeat
failing cases up to three times to identify intermittent rendering differences;
reproducible failures still fail CI. Neither run validates physical-device input.

### ARM64 final build evidence

OpenAxis retry: https://github.com/rotatrix/openscad/actions/runs/36621126174
Upstream retry: https://github.com/rotatrix/openscad/actions/runs/36621129467
Native OpenAxis DMG (10-day retention):
https://github.com/rotatrix/openscad/actions/runs/36621126174/artifacts/11059646213

The final package passed all bundled Mach-O ARM64 architecture checks, native
packaged STL export and DMG integrity verification. The full suite is NOT green:
27 tests still failed after up to three attempts. Every one of these test names
also failed in at least one of the two exact upstream-source comparison runs.
The latest upstream run failed 26; preview-manifold_rotate_extrude-hole passed
there after failing in the earlier upstream run. The earlier OpenAxis-only
highlight-modifier failure did not recur. This demonstrates inherited failures
and run variability, not proof of identical images or complete GUI correctness.

Automatic retries are stopped because repeated full builds have established the
upstream failure overlap. No failing test has been excluded to produce a green
status. ARM64 is available as a development test package; complete rendering and
physical-device acceptance remain outstanding before maintained release promotion.


## Picking and diagnostic repair (2026-09-29)

User testing found nonfunctional picks, clipped diagnostic rows and absent
semantic colors and graphics. The initial integration did not meet the SDK
rendering contract; the earlier compilation results did not validate these paths.
The fork workflow and its linked navigation, validation, coordinates, picking,
diagnostics, rendering, lifecycle, settings and pivot guidance were reviewed.
The repair stays within the previously authorized navigation/diagnostic scope.

- Cursor and independent center picks render the current native geometry into a
  single-sample depth/stencil framebuffer. This avoids depth reads from Qt's
  multisample display framebuffer. Depth state is established explicitly, and
  unprojection uses the native cached world-camera matrices, not renderer state.
  Axes, crosshairs, measurement decorations, pivots and diagnostic graphics are
  excluded. Native context/framebuffer and display state are restored afterward.
- All SDK world segments are rendered with their supplied colors, opacity and
  widths and the current camera. Screen samples use labeled crosshairs; labels
  keep their supplied names and multiline layout. Positions scale only once.
- Complete text rows preserve the SDK palette in a resizable, scrollable native
  dock panel. This companion panel avoids truncating evidence in a small viewport;
  geometry remains in the viewport. Text updates preserve scroll position.
- The authoritative pivot is independent of diagnostics: a lime disc with a black
  annulus, constant logical-pixel size and complementary depth passes (opaque
  exposed fragments, 23% opacity behind geometry), without depth writes.
- OpenSCAD measurement points/edges are not a selected solid/body. Selection-only
  surface picks and selection bounds remain unavailable; they are not substituted
  with whole-model picks. Per-hit body bounds are omitted because the depth buffer
  does not identify a body. Model bounds remain available separately.

Validation for this repair is pending platform compilation and actual host checks.
Required native checks: hit/miss and independent center picks in preview and full
render, perspective/orthographic projections, translated/rotated models, MSAA on
and off, high DPI, portrait/landscape windows, colored multiline labels, scrollable
rows in narrow docks, native camera movement, partially occluded pivots, and
cleanup after disconnect, disable, scene replacement and window close. Prior CI
and camera-math tests do not establish these rendering checks.

### Repair CI results

Repair source: a68c7e3211d700309a5711e00d0d2ca67b397aa6.
Linux release matrix (36626029977), release tests (36626029852), examples
(36626029480), all experimental Linux jobs (36626029782), Windows Qt5/Qt6
(36626029804), and Intel macOS (job 109603106175 in 36626029648) passed.
ARM64 compiled, packaged and passed native architecture/export/DMG checks;
2672 of 2694 tests passed, including openaxis_camera. Its 22 failing image tests
are a subset of the documented upstream failure baseline. ARM64 CI is not green.

Repair packages (10-day artifact retention):
- ARM64 macOS: https://github.com/rotatrix/openscad/actions/runs/36626029648/artifacts/11060838738
- Intel macOS: https://github.com/rotatrix/openscad/actions/runs/36626029648/artifacts/11061707268
- Windows Qt6: https://github.com/rotatrix/openscad/actions/runs/36626029804/artifacts/11061303949
- Windows Qt5: https://github.com/rotatrix/openscad/actions/runs/36626029804/artifacts/11061906821
- Ubuntu 24.04 Qt6: https://github.com/rotatrix/openscad/actions/runs/36626029782/artifacts/11061228943

Artifact availability and nonzero size were verified. Native interactive picking,
diagnostic readability/colors/geometry and device acceptance remain pending;
these checks are not established by the successful builds or camera math tests.
Build monitoring is disabled after completing this CI review.
