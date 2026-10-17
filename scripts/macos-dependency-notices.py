#!/usr/bin/env python3
"""Collect installed Homebrew notices after macdeployqt, before final signing.

Usage: python3 scripts/macos-dependency-notices.py build/OpenSCAD.app
Missing notices are reported as errors; this does not audit static dependencies.
"""

import argparse
from functools import lru_cache
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys


MAGIC = {bytes.fromhex(s) for s in (
    "feedface", "cefaedfe", "feedfacf", "cffaedfe", "cafebabe", "bebafeca",
    "cafebabf", "bfbafeca",
)}


def macho(path):
    if not path.is_file() or path.is_symlink():
        return False
    with path.open("rb") as stream:
        return stream.read(4) in MAGIC


@lru_cache(maxsize=None)
def uuids(path):
    result = subprocess.run(["/usr/bin/dwarfdump", "--uuid", str(path)],
                            check=True, capture_output=True, text=True)
    return set(re.findall(r"UUID: ([0-9A-Fa-f-]+)", result.stdout))


def notice_files(keg):
    # Do not copy build-tool licenses buried under lib/cmake or man pages.
    roots = [keg, keg / "LICENSES", keg / "share/licenses", keg / "share/doc"]
    found = set()
    for root in roots:
        if not root.is_dir():
            continue
        paths = root.iterdir() if root == keg else root.rglob("*")
        for path in paths:
            if path.is_file() and (root == keg / "LICENSES" or re.match(
                    r"^(licen[cs]e|copying|copyright|notice)([._-].*)?$",
                    path.name, re.IGNORECASE)):
                found.add(path)
    return sorted(found)


def source_url(keg):
    sbom = keg / "sbom.spdx.json"
    if sbom.is_file():
        for package in json.loads(sbom.read_text()).get("packages", []):
            url = package.get("downloadLocation", "")
            if (package.get("name") == keg.parent.name and url.startswith(("https://", "http://"))
                    and "ghcr.io/" not in url):
                return url
    # Older installed kegs predate Homebrew SBOMs. Their saved formula is
    # version-specific; accept only a literal top-level source URL.
    formula = keg / ".brew" / (keg.parent.name + ".rb")
    if formula.is_file():
        match = re.search(r'^  url "(https?://[^"\n]+)"', formula.read_text(), re.MULTILINE)
        if match and "#{" not in match.group(1):
            return match.group(1)
    return None


def collect(app, cellar, destination, covered=None):
    app, cellar = app.resolve(), cellar.resolve()
    binaries = [p for p in app.rglob("*") if macho(p)
                and any(part in {"Frameworks", "PlugIns", "plugins"} for part in p.parts)]
    names = {p.name for p in binaries}
    candidates = {}
    for path in cellar.rglob("*"):
        if path.name in names and macho(path.resolve()):
            candidates.setdefault(path.name, []).append(path.resolve())
    matched = {}
    errors = []
    for binary in sorted(binaries):
        if binary.name in (covered or {}):
            notice = (app / covered[binary.name]).resolve()
            if not notice.is_relative_to(app) or not notice.is_file() or notice.stat().st_size == 0:
                errors.append(f"Missing separately supplied notice for {binary.name}: {notice}")
            continue
        identity = uuids(binary)
        matches = [p for p in candidates.get(binary.name, [])
                   if identity and identity <= uuids(p)]
        if not matches:
            errors.append(f"No exact installed keg match: {binary.relative_to(app)}")
            continue
        kegs = {cellar.joinpath(*p.relative_to(cellar).parts[:2]) for p in matches}
        if len(kegs) != 1:
            errors.append(f"Ambiguous installed keg: {binary.relative_to(app)}")
            continue
        keg = kegs.pop()
        matched.setdefault(keg, []).append(binary.relative_to(app))

    destination.mkdir(parents=True, exist_ok=True)
    lines = ["Homebrew dependencies bundled with OpenSCAD", "",
             "Versions and source references below identify the installed kegs.",
             "See adjacent files for their license and copyright notices.",
             "This inventory does not cover statically compiled dependencies.", ""]
    for name, notice in sorted((covered or {}).items()):
        lines.append(f"Separately supplied notice: {name}: {notice}")
    for keg, members in sorted(matched.items()):
        label = f"{keg.parent.name}/{keg.name}"
        files = notice_files(keg)
        source = source_url(keg)
        lines.extend([label, f"Source: {source or 'MISSING'}", "Bundled files:"])
        lines.extend(f"  {member}" for member in members)
        if not source:
            errors.append(f"Missing source reference: {label}")
        if not files:
            errors.append(f"Missing installed license notices: {label}")
        lines.append("Notices:")
        for path in files:
            relative = path.relative_to(keg)
            target = destination / label / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
            lines.append(f"  {label}/{relative}")
        # Qt's general license texts alone do not cover embedded contributions.
        if keg.parent.name == "qt" or keg.parent.name.startswith("qt"):
            lines.append("Qt attribution reference: https://doc.qt.io/qt-"
                         + ".".join(keg.name.split(".")[:2]) + "/licenses-used-in-qt.html")
            if not (keg / "LICENSES").is_dir():
                errors.append(f"Missing Qt LICENSES directory: {label}")
            lines.append("Qt embedded third-party attribution texts require separate coverage.")
        lines.append("")
    if errors:
        lines.extend(["UNRESOLVED (not a complete notice bundle):", *errors, ""])
    (destination / "README.txt").write_text("\n".join(lines))
    for error in errors:
        print(error, file=sys.stderr)
    print(f"Collected notices for {len(matched)} kegs into {destination}")
    return 1 if errors else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    parser.add_argument("--cellar", type=Path)
    parser.add_argument("--covered-library", action="append", default=[],
                        metavar="BINARY=APP_RELATIVE_NOTICE",
                        help="Non-Homebrew library covered by a separate nonempty notice file")
    args = parser.parse_args()
    cellar = args.cellar or Path(subprocess.check_output(
        ["brew", "--cellar"], text=True).strip())
    app = args.app.resolve()
    if not (app / "Contents/MacOS/OpenSCAD").is_file():
        parser.error("Expected a deployed OpenSCAD.app")
    covered = {}
    for item in args.covered_library:
        name, separator, notice = item.partition("=")
        if not separator or not name or not notice or "/" in name:
            parser.error("--covered-library requires BINARY=APP_RELATIVE_NOTICE")
        covered[name] = notice
    return collect(app, cellar.resolve(), app / "Contents/Resources/licenses/Homebrew", covered)


if __name__ == "__main__":
    sys.exit(main())
