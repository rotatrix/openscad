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
The integration connects automatically. The View menu provides the OpenAxis
Diagnostics toggle. Navigation shares the native camera and invalidates gestures
on focus loss or viewport size/DPI changes; re-rendering does not invalidate them.

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

### Follow-up: diagnostic GL isolation and viewport text

macOS user testing found incorrect CSG depth ordering after diagnostics opened.
Review found the GL state guard ended before QPainter painted screen markers
onto QOpenGLWidget. That painter uses Qt's GL engine and can alter state consumed
by the next native render. Screen markers now paint on a mouse-transparent raster
QWidget child; text uses a separate raster QTextEdit child. Neither screen layer
paints into the native GL framebuffer. World segments and pivots retain their
existing guarded GL rendering. The shared screen renderer has an offscreen Qt
regression test for rendering without a GL context, marker placement and cleanup.
This does not substitute for macOS native OpenCSG ordering acceptance.

The earlier dock was an implementation choice for overflow, not a host limitation
or user-requested UI. It has been removed. Text is again over the viewport, with
semantic colors, word wrapping and scrolling, bounded to 460 pixels wide and
one-third viewport height (maximum 220 pixels). It preserves scroll position and
does not change camera aspect. Screen marker positions remain unchanged.
Compilation, the new raster test and native macOS visual verification are pending.

### GL-isolation repair validation (2026-09-30)

Application source d4bf8a711b4b58040f3bc3ebf9f75d1cbcfb883f passed Linux release
matrix (36645322939), release tests (36645323021), examples (36645323012), Linux
experimental tests (36645323005), Windows Qt5/Qt6 (36645323093), and Intel macOS
(job 109666828592 in 36645322980). ARM64 package architecture/export/DMG checks
passed; openaxis_camera and openaxis_overlay passed. Its full suite retained 26
image failures out of 2695 tests, including an additional openscad-cameye case.

The additional camera case passed the upstream full suite (36647981385), then
reproduced intermittently in BOTH existing packages on one ARM64 runner
(36650581496): upstream passed the reference on 3/5 attempts, repair on 2/5.
Only 1/5 paired renders matched under the upstream comparator. This establishes
upstream reproduction and nondeterminism, not pixel equivalence or no regressions.
The diagnostic workflow initially reported success because tee masked Python's
nonzero exit; pipefail is now explicit. Raw comparison results, not that green
workflow status, are the evidence. No tolerance or image baseline was changed.

Latest repair packages (10-day retention):
- ARM64 macOS: https://github.com/rotatrix/openscad/actions/runs/36645322980/artifacts/11068309570
- Intel macOS: https://github.com/rotatrix/openscad/actions/runs/36645322980/artifacts/11068898795
- Windows Qt6: https://github.com/rotatrix/openscad/actions/runs/36645323093/artifacts/11069596923
- Windows Qt5: https://github.com/rotatrix/openscad/actions/runs/36645323093/artifacts/11069290945
- Ubuntu: https://github.com/rotatrix/openscad/actions/runs/36645323005/artifacts/11069530481

Build verification is finished and monitoring stopped. Interactive CSG depth
ordering after diagnostic toggling, viewport text readability, picking and
physical-device acceptance remain outstanding; the raster test does not exercise
native OpenCSG rendering. ARM64 full CI remains failing as documented above.

## Integration assessment and coverage (2026-09-30)

Assessment baseline: application source d4bf8a711 on upstream c2c321215.
Documentation read: OpenAxis checkout 4621660 (`$OPENAXIS/docs/src/content/docs`),
including the revised settings-and-controls and diagnostic-rendering guidance.

Doc abbreviations: CL = guide/validation.md, PP = guide/picking-pivots.mdx,
CO = concepts/coordinates.md, NH = reference/navigation-hosts.mdx,
CI = guide/concurrent-input.mdx, DR = reference/diagnostic-rendering.md,
DG = guide/diagnostics.mdx, PA = recommendations/pivot-appearance.md,
SC = recommendations/settings-and-controls.md, LC = reference/connection-lifecycle.mdx,
DT = guide/dynamic-tags.mdx, SL = guide/session-logs.mdx, FC = guide/free-camera.mdx,
OM = guide/object-manipulation.mdx, 2D = guide/2d-navigation.mdx.
Req = SDK requirement; Rec = SDK recommendation; Fork = maintained-fork policy.
Bare `:N` line numbers refer to src/gui/OpenAxisController.cc.

| Capability | SDK requirement or recommendation | Native API | Proposed support | Limitation or open question | Validation method |
| --- | --- | --- | --- | --- | --- |
| 3D orbit/pan/zoom, main viewport | Req: planning "What should work"; CL test row 1 | One `QGLView` per `MainWindow`; `Camera` (object_rot, object_trans, viewer_distance, fov) | Included (`Impl::apply_pose`, :200) | None known | Device: direction and scale in both projections |
| 2D pan/zoom (`viewspace.2d`) | Rec: 2D "Describe a 2D viewport" | 2D designs render in the same orbitable 3D view | Inapplicable: no fixed-orientation 2D editor | `viewspace.3d` behaviour is correct for 2D models | n/a |
| Free camera | Rec: FC; SC control table | Camera API permits it; the app is a model inspector | Omitted (decided 2026-09-30) | Orbit-only: no mode preference, translation scale or free-camera tags (SC) | Decision at scope approval |
| Object manipulation | Rec when accept/cancel/undo exist: OM | Geometry is script-defined; no interactive transform edit or undo | Inapplicable | Driving Customizer parameters would be Axis Streaming | n/a |
| Axis Streaming | Alternative interface: index "Choose an interface" | Customizer (`ParameterWidget`) | Out of scope | Possible future feature | n/a |
| Camera read/write conversion | Req: CO "Camera axes and handedness", "Projection…"; CL "Pose" | `GLView::setupCamera` (src/glview/GLView.cc:125): gluLookAt(0,-d,0), then Rx·Ry·Rz; vertical-fov gluPerspective (:132); ortho half-height d·tan(fov/2) (:137) | Included (OpenAxisCamera.h `write`, `compare`) | Decided: Rotatrix must not change mouse orbit behaviour. `write()` keeps the native orbit centre (`-object_trans`) at its depth on the new view axis. Perspective motion along the axis changes `viewer_distance`; a centre at or behind the eye falls back to the previous distance. Orthographic writes normalize eye depth (distance follows the extent), so `compare` ignores that axial difference only. Equivalent but non-canonical vpr values from Eigen `eulerAngles` remain cosmetic. | tests/openaxis/camera.cc: dolly, turn, past-centre, orthographic and comparison checks (passed locally with MSVC). Device: dolly, then mouse-orbit about the same centre |
| Projection changes | CO "Projection" | `Camera::setProjection` | Write path accepts the projection in the pose | Rotatrix does not switch projection, so no menu/setting sync is needed | Native View-menu switches are observed as native changes |
| Viewport aspect, resize, DPI | Req: CO "Screen coordinates"; CL test row 2 | `GLView::resizeGL` aspect = w/h (GLView.cc:109); `devicePixelRatioF` | Included | Resize or DPI change cancels the active gesture (intended) | Manual: portrait, landscape, mixed-DPI monitors |
| Viewport cursor | Req: CO normalized cursor; unavailable outside | `QCursor::pos`, `QApplication::widgetAt` | Included (:232) | Captured live at resolve time. The `QTextEdit` overlay makes the cursor over it count as outside; removed by the text-row change below | Manual: cursor outside the viewport and in the former panel area |
| Surface picks: cursor and centre | Req when supported: PP "Query handling", "Choosing a native picking operation" | Current: full shaded `GLView::paintGL` into a single-sample FBO for **each** pick fact (:241–313). Native: `MouseSelector::select` (src/gui/MouseSelector.cc:95) renders node-ID colours with depth into an FBO. OpenCSG select pass keeps CSG-correct depth (`OpenCSG::render`, then ID draw at `GL_EQUAL`, src/glview/preview/OpenCSGRenderer.cc:130–157) | Included (decided 2026-09-30; `render_pick`, `pick_at`): one lazy offscreen render with the native select shader writes leaf-index colour and CSG-correct depth at device pixels. Cursor and centre read depth and ID from it and un-project with the matrices of that pass. Reused while camera, renderer, `sceneRevision` and size are unchanged | Cost is one geometry pass per gesture start instead of up to two shaded passes per query; not yet measured. OpenCSG's multipass cost per product remains, and a pick-matrix sub-viewport would not remove it. `%`/`#` products still occlude | Performance summary and query timings on a heavy OpenCSG model, before and after; hit/miss/centre in preview, thrown-together and render modes |
| Per-hit body bounds | Rec: PP "Returned optional bounds describe the hit object or body" | ID = `CSGLeaf::index` (OpenCSGRenderer.cc:240); leaf world bbox = matrix × polyset bbox (src/core/csgnode.cc:145); body = `CSGProduct::getBoundingBox` (csgnode.cc:267); products in `MainWindow::rootProduct` (MainWindow.h:453) | Included in OpenCSG and thrown-together preview (decided 2026-09-30; `body_bounds`). The hit leaf maps to the rendered products containing it whose bounds contain the hit point (tolerance: 1e-3 of body diagonal + 1e-5 of view distance); their bounds are merged. Root products are searched before highlight and background products. F6 full render: bounds unavailable, point still returned | A cavity surface carries the *subtracted* leaf's ID, so leaf bounds would describe the cutter. Hence product bounds. One leaf can appear in several normalized products. F6 render (`PolySetRenderer`) writes no IDs; the result is one geometry, so return unavailable (or model bounds, if approved) | Native experiment: union, difference cavity, intersection, and a leaf shared across products; diagnostics show the hit bounds box |
| Selection-only picks, selection bounds | Req when a selection exists: PP query table | No persistent geometry selection. Right-click `pickObject` (src/gui/QGLView.cc:660) only opens a source-backtrace menu (MainWindow.cc:2143). Measurement `selected_obj` holds points/edges | Inapplicable (unavailable, :314) | The ID pick could later back a real selection feature; out of scope | Diagnostics show *unavailable*, not *skipped* |
| Model bounds | Req when available: PP query table | `Renderer::getBoundingBox` | Included (:236) | Follows the current renderer | Diagnostics vs model extents |
| World orientation | Req: CO intro | Fixed Z-up, right-handed; Front looks along +Y | Included, constant (:228) | None | Diagnostic row |
| Context identity / `document.id` | Req: NH "Captured context"; CL "Target" | `GLView::setRenderer` (GLView.cc:78) runs on every preview, render and view-mode switch (MainWindow.cc:2700, 2719, 2749) | Included (decided 2026-09-30): the context is the viewport plus size/DPR. `document.id` is the active editor tab (`MainWindow::activeEditor`), unchanged by re-render. `sceneRevision` only invalidates the cached pick render | Picks and bounds are already snapshotted per query | Manual: navigate during animation, auto-reload, F5/F6 |
| Native input reconciliation | Req: CI "Change notifications"; NH "Application events" | Mouse and wheel via `QGLView::rotate/translate/zoom*` (QGLView.cc:499–580); `InputDriverManager` via `MainWindow::onRotateEvent` (MainWindow.cc:470) | Included by polling camera state on each paint (:156–163) | Coexistence with a native 3D-mouse driver is untested | Manual: mouse and SpaceMouse during a gesture, including release |
| Threading / scheduler | Req: NH "Scheduler and lifecycle" | Queued `QTimer::singleShot(0, ctx, fn)` | Included (:31) | `capture_context` → `refresh()` can call `session.cancel` reentrantly; relies on the C++ reentrancy guarantee | SDK debug log |
| Focus and metadata | Req: DT "Report focus"; LC "Metadata is desired state" | `isActiveWindow`, `activeModalWidget` | Included; tags `app.openscad`, `workspace.modeling` | One client per window, same PID | Manual: two windows, modal dialogs |
| Pivot presentation | Fork: initial deliverable; Rec: PA | Legacy GL after the scene | Included (:357–395) | Relies on scene depth after OpenCSG's final pass | Manual: occluded pivot, zoom, both projections, high DPI |
| Diagnostic world segments | Req: DR "Required behavior" | Legacy GL with the current camera | Included (:345–356) | None | Manual: reprojection during mouse orbit |
| Diagnostic screen markers | Req: DR crosshair and labels | Mouse-transparent raster `QWidget` (OpenAxisOverlay.h) | Included | Labels clamp inside the viewport near edges | tests/openaxis/overlay.cc; native OpenCSG ordering |
| Diagnostic text rows | Req: DR "Draw supplied text rows and graphics directly in the viewport" (revised) | Same raster overlay, which paints no GL state | Included (decided 2026-09-30): status and word-wrapped rows are painted on the mouse-transparent `ScreenOverlay` under the crosshairs, with text-sized backdrops. Rows flow into further 420 px columns; if none remain, a row states how many are not shown | The `QTextEdit` was an overflow workaround, not a host limitation | Offscreen overlay test for row layout; manual readability on light and dark schemes |
| Diagnostic expiry and context | Req: DG "Application display" | Qt timers | Included (:435) | None | Statuses expire without input |
| Diagnostics on/off parity | Req: CL test last row | — | Included | macOS CSG ordering fix (d4bf8a711) awaits native confirmation | Manual A/B |
| Control: OpenAxis diagnostics | Req: SC control table ("every Navigation integration") | View menu (`menu_View`) | Included | — | Manual |
| Control: connect/disconnect and status | SC: plugins only; otherwise status in diagnostics and logs | Built into the application, not a plugin | Included (decided 2026-09-30): the "OpenAxis Navigation" toggle is removed; the integration connects automatically. Status stays in the diagnostic text (:421) and the lifecycle logs | Removal of the feature is the build option `ENABLE_OPENAXIS` | Manual: start before and after Rotatrix; restart Rotatrix |
| Mode controls | SC: only for supported modes | — | None (orbit-only) | Revisit if free camera is approved | n/a |
| Session logs | Rec: SL "Configure your integration" | `DiagnosticLog::configure("openscad")` (:56) | Included | One process logger shared by all windows, configured with `openscad_displayversionnumber` and closed after the last window's connection stops and session closes | Session header in `%LOCALAPPDATA%/Rotatrix/logs` |
| Lifecycle and shutdown | Req: LC "Shutdown order" | `~QGLView` resets the controller before GL teardown | Included | None known | Manual: close mid-gesture and during retry |
| Packaging and platforms | Fork: CI section | Upstream nightly matrix | Included; see CI evidence above | ARM64 inherits upstream image failures | CI artifacts |

### Decisions (recorded 2026-09-30)

1. **Free camera is omitted.** OpenSCAD inspects models from outside; the
   integration is orbit-only and has no mode controls.
2. **Rotatrix must not change mouse orbit behaviour.** Camera writes keep
   OpenSCAD's orbit centre and change viewer distance instead.
3. **Re-rendering is not a document change.** `document.id` identifies the editor
   tab, and the navigation context is the viewport; preview, render, view mode,
   animation and reload do not cancel gestures.
4. **Picking uses the native select pass.** One render per camera/scene state
   provides both surface depth and leaf index. The earlier full shaded render
   for each pick fact is removed because of its cost on heavy previews.
5. **Per-hit bounds describe the CSG product (visible body)**, not the leaf, so
   cavity hits do not report the cutter. They are unavailable in F6 render,
   which has no per-object IDs.
6. **Diagnostics are drawn directly in the viewport** (revised DR/SC guidance) on a
   raster, mouse-transparent overlay. The `QTextEdit` overflow workaround is removed.
7. **Controls follow the revised SC guidance for a built-in integration:** a single
   View-menu "OpenAxis Diagnostics" toggle. Connection status is shown in the
   diagnostics and session logs; there is no connect/disconnect control.
8. **Projection sync is out of scope:** Rotatrix does not switch projection.
9. **Selection-only picks and selection bounds remain unavailable:** OpenSCAD has no
   persistent geometry selection.

### Completion status against this table

Implemented: all rows marked Included. Automated tests:
- Camera round-trip, dolly/orbit-centre and orthographic comparison checks
  (passed locally, MSVC).
- Raster overlay text and marker checks (to run in CI).

Not yet done:
- Full application compilation is pending CI.
- Manual verification in the host: **none recorded**.
- Physical-device testing: **none**.
