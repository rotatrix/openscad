#!/usr/bin/env python3
"""Classify the documented nightly-2026.09.01 ARM64 preview limitations.

Run the unchanged suite first. This accepts comparison mismatches only, never
process failures. Remove/reassess this list when changing the upstream baseline.
"""
import re
import sys
import xml.etree.ElementTree as ET

# Observed in fork run 38993220854 or exact-upstream run 38988995233,
# including intermittent cases that recovered on repetition. See release notes.
IMAGE_FAILURES = set("""
preview-cgal_intersection_for-tests
preview-cgal_nullspace-difference
preview-cgal_issue911
preview-cgal_rotate-empty-bbox
preview-cgal_internal-cavity
preview-cgal_rotate_extrude-hole
preview-cgal_issue1105d
preview-cgal_difference-tests
preview-manifold_intersection_for-tests
preview-manifold_minkowski3-erosion
preview-manifold_nullspace-difference
preview-manifold_issue911
preview-manifold_rotate-empty-bbox
preview-manifold_internal-cavity
preview-manifold_rotate_extrude-hole
preview-manifold_issue1105d
openscad-imgstretch2_camera-tests
openscad-cameye_front_camera-tests
openscad-cameye_back_camera-tests
openscad-cameye_left_camera-tests
openscad-cameye_right_camera-tests
openscad-cameye_top_camera-tests
openscad-cameye_bottom_camera-tests
openscad-camcenter_camera-tests
openscad-cameye_camera-tests
openscad-cameye2_camera-tests
lazyunion-preview_lazyunion-intersection-for
lazyunion-preview_lazyunion-difference-for
""".split())
SVG_FAILURES = {f"{mode}_spec-paths-arcs01" for mode in
                ("export-svg", "export-svg-fill-stroke", "export-svg-fill-only")}


def classify(report, exit_code):
    if exit_code not in (0, 8):
        raise ValueError(f"CTest infrastructure/process failure: {exit_code}")
    cases = report.findall("testcase")
    if (not cases or int(report.get('tests', '-1')) != len(cases)
            or len({c.get('name') for c in cases}) != len(cases)):
        raise ValueError("Missing or duplicate test results")
    for required in ("openaxis_camera", "openaxis_overlay"):
        if not any(c.get("name") == required and c.get("status") == "run"
                   and c.find("failure") is None for c in cases):
            raise ValueError(f"Required test did not pass: {required}")
    accepted = []
    for case in cases:
        name = case.get("name")
        failure = case.find("failure")
        if case.find("error") is not None:
            raise ValueError(f"Test error: {name}")
        if failure is None:
            if case.get("status") not in ("run", "disabled"):
                raise ValueError(f"Test did not run: {name}")
            continue
        output = case.findtext("system-out", "")
        # CTest labels normal exit-code failures as Failed. Reject
        # timeout, signal, launch errors and the test driver's subprocess errors.
        if (case.get("status") != "fail" or failure.get("message") != "Failed") or re.search(
                r"error[: ]|exception|traceback|segmentation|abort|could not read",
                output, re.IGNORECASE):
            raise ValueError(f"Non-comparison failure: {name}")
        image = name in IMAGE_FAILURES and re.search(
            r"Image comparison return: 1 output: [0-9.]+% of 3x3 blocks differ", output)
        svg = name in SVG_FAILURES and all(marker in output for marker in
            ("text comparison:", "expected textfile:", "actual textfile:", "\n---", "\n+++", "\n@@"))
        if not (image or svg):
            raise ValueError(f"Unrecognized failure: {name}")
        accepted.append(name)
    if bool(accepted) != (exit_code == 8):
        raise ValueError("CTest exit code and failure report disagree")
    if int(report.get("failures", "-1")) != len(accepted):
        raise ValueError("Incomplete failure report")
    return accepted


if __name__ == "__main__":
    try:
        accepted = classify(ET.parse(sys.argv[1]).getroot(), int(sys.argv[2]))
    except (ValueError, OSError, ET.ParseError) as error:
        sys.exit(str(error))
    for name in accepted:
        print(f"::warning::Approved ARM64 preview comparison limitation: {name}")
    print(f"ARM64 results: {len(accepted)} documented comparison limitations; other tests passed.")
