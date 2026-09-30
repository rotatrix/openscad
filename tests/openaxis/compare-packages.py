"""Diagnose existing packages; do not alter reference images or tolerances."""
import importlib.util
from pathlib import Path
import json
import os
import subprocess

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("image_compare", root / "tests/image_compare.py")
comparison = importlib.util.module_from_spec(spec)
spec.loader.exec_module(comparison)
output = root / "comparison"
output.mkdir(exist_ok=True)
expected = root / "tests/regression/openscad-cameye/camera-tests-expected.png"
env = dict(os.environ, OPENSCAD_FONT_PATH=str(root / "tests/data/ttf"))
results = []
for attempt in range(5):
    images = {}
    for variant in ("upstream", "repair"):
        image = output / f"{variant}-{attempt}.png"
        app = root / f"packages/{variant}/OpenSCAD.app/Contents/MacOS/OpenSCAD"
        subprocess.run([str(app), str(root / "tests/data/scad/3D/misc/camera-tests.scad"),
                        "--imgsize=500,500", "--camera=120,80,60,0,0,0", "-o", str(image)],
                       env=env, check=True)
        images[variant] = image
        passed = comparison.CompareImageFiles(str(expected), str(image))
        results.append(dict(attempt=attempt, variant=variant, reference_passed=passed))
    matched = comparison.CompareImageFiles(str(images["upstream"]), str(images["repair"]))
    results.append(dict(attempt=attempt, pair_matched=matched))
(output / "results.json").write_text(json.dumps(results, indent=2))
print(json.dumps(results, indent=2))
if not all(r.get("pair_matched", True) for r in results):
    raise SystemExit("Packaged repair and upstream images differ; inspect retained evidence")
