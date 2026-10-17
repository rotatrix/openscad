#!/usr/bin/env python3
"""Collect Homebrew runtime dependency notices before signing the macOS app.

Run before or after macdeployqt, but before signing:
  python3 scripts/macos-dependency-notices.py build/OpenSCAD.app
Source downloads stay in ignored deps/notice-sources; only notices enter the app.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import posixpath
import re
import shutil
import subprocess
import tarfile

# Match the libraries installed by macosx-build-homebrew.sh, excluding build and
# test tools and the unused system OpenCSG/GLEW (this build uses builtin OpenCSG).
FORMULAE = "qt qscintilla2 lib3mf boost eigen cgal glib freetype libzip fontconfig harfbuzz double-conversion tbb".split()
# Qt modules deployed for OpenSCAD, including imageformat plugins and Qt PDF.
QT_MODULES = {"qtbase", "qt5compat", "qtdeclarative", "qtimageformats",
              "qtmultimedia", "qtsvg", "qtvirtualkeyboard", "qtwebengine"}
NOTICE = re.compile(r"^(licen[cs]e|copying|copyright|notice)(s|[._-].*)?$", re.I)


def run(*args):
    return subprocess.check_output(args, text=True).strip()


def source_info(keg):
    sbom = keg / "sbom.spdx.json"
    if sbom.is_file():
        for package in json.loads(sbom.read_text()).get("packages", []):
            url = package.get("downloadLocation", "")
            sums = {c["algorithm"]: c["checksumValue"] for c in package.get("checksums", [])}
            if (package.get("name") == keg.parent.name and url.startswith(("https://", "http://"))
                    and "ghcr.io/" not in url and "SHA256" in sums):
                return url, sums["SHA256"]
    # Older kegs retain their exact formula even when they have no SBOM.
    formula = (keg / ".brew" / (keg.parent.name + ".rb")).read_text()
    url = re.search(r'^  url "(https?://[^"\n]+)"', formula, re.M)
    checksum = re.search(r'^  sha256 "([0-9a-f]{64})"', formula, re.M)
    if not url or not checksum or "#{" in url[1]:
        raise RuntimeError(f"No exact source URL/checksum in installed keg: {keg}")
    return url[1], checksum[1]


def installed_notices(keg):
    paths = [p for p in keg.iterdir() if p.is_file() and NOTICE.match(p.name)]
    for relative in ("LICENSES", "share/licenses", "share/doc"):
        root = keg / relative
        if root.is_dir():
            paths += [p for p in root.rglob("*") if p.is_file()
                      and (relative == "LICENSES" or NOTICE.match(p.name))]
    return sorted(set(paths))


def download(url, checksum, cache):
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / checksum
    if not archive.is_file():
        partial = archive.with_suffix(".partial")
        urls = [url]
        if "download.qt.io/official_releases/" in url:
            urls.append(url.replace("/official_releases/", "/archive/"))
        for location in urls:
            result = subprocess.run(["curl", "--fail", "--location", "--silent", "--show-error",
                                     "--retry", "2", "--output", str(partial), location])
            if result.returncode == 0:
                partial.replace(archive)
                break
        else:
            raise RuntimeError(f"Could not download {url}")
    with archive.open("rb") as stream:
        actual = hashlib.file_digest(stream, "sha256").hexdigest()
    if actual != checksum:
        raise RuntimeError(f"Source checksum mismatch: {archive}")
    return archive


def source_notices(archive, qt=False, monolithic=True):
    """Read only notice files, never extract archive paths or source archives."""
    notices, attributions, optional = {}, {}, set()
    with tarfile.open(archive, "r|*") as source:
        for member in source:
            path = PurePosixPath(*PurePosixPath(member.name).parts[1:])
            if not member.isfile() or not path.parts or ".." in path.parts:
                continue
            if qt and monolithic and len(path.parts) > 1:
                if path.parts[0] not in QT_MODULES and path.parts[0] != "LICENSES":
                    continue
                if path.parts[1] in {"tests", "examples", "tools"}:
                    continue
            elif qt and path.parts[0] in {"tests", "examples", "tools"}:
                continue
            if not qt and str(path) == "sqlite3.c":
                headers = re.findall(rb"/\*.*?\*/", source.extractfile(member).read(4096), re.S)
                header = next((h for h in headers if b"disclaims copyright" in h), None)
                if header is None:
                    raise RuntimeError("SQLite source is missing its public-domain notice")
                notices["SQLite-PUBLIC-DOMAIN.txt"] = header
            elif path.name == "qt_attribution.json":
                # Some upstream attribution descriptions contain literal newlines.
                records = json.load(source.extractfile(member), strict=False)
                attributions[str(path)] = records if isinstance(records, list) else [records]
            elif ((NOTICE.match(path.name) and path.suffix not in
                   {".py", ".js", ".ts", ".c", ".cc", ".cpp", ".h", ".cmake"})
                  or "LICENSES" in path.parts):
                data = source.extractfile(member).read()
                if data.strip():
                    notices[str(path)] = data
            elif qt and "3rdparty" in path.parts and path.name.lower().startswith("readme"):
                # Chromium/PDFium and IJG use README files for some notices and
                # for pointers to license files with nonstandard names.
                data = source.extractfile(member).read()
                if re.search(rb"copyright|licen[cs]e|permission", data, re.I):
                    notices[str(path)] = data
                if path.name == "README.chromium":
                    for entry in re.findall(r"^License File:[ \t]*(.+)$", data.decode(errors="replace"), re.M):
                        for name in entry.split(","):
                            name = name.strip()
                            if name == "NOT_SHIPPED":
                                continue
                            parent = str(path.parent)
                            if name.startswith("//"):
                                parent = str(path).split("/chromium/", 1)[0] + "/chromium"
                                name = name[2:]
                            optional.add(posixpath.normpath(posixpath.join(parent, name)))
    # Qt LicenseFile sometimes refers to README or another nonstandard filename.
    # Chromium READMEs also describe optional submodules absent from Qt's source
    # release. Collect their referenced files when present; Qt attribution
    # records, in contrast, must always resolve to nonempty license texts.
    required = set()
    for path, records in attributions.items():
        for record in records:
            for name in record.get("LicenseFile", "").split():
                target = posixpath.normpath(posixpath.join(posixpath.dirname(path), name))
                if target.startswith("../") or target.startswith("/"):
                    raise RuntimeError(f"Unsafe Qt license path: {target}")
                required.add(target)
    missing = (required | optional) - notices.keys()
    if missing:
        with tarfile.open(archive, "r|*") as source:
            for member in source:
                path = str(PurePosixPath(*PurePosixPath(member.name).parts[1:]))
                if path in missing and member.isfile():
                    data = source.extractfile(member).read()
                    if data.strip():
                        notices[path] = data
        missing = {name for name in required if not notices.get(name, b"").strip()}
    if missing:
        raise RuntimeError("Missing referenced Qt notices: " + ", ".join(sorted(missing)))
    if not notices:
        raise RuntimeError(f"Missing source license texts: {archive}")
    if qt:
        lines = ["Qt third-party attributions", "",
                 "Includes additional components from shared module sources.",
                 "QtPdf uses QtWebEngine's source tree; this does not imply that",
                 "the QtWebEngine browser framework is bundled with OpenSCAD.", ""]
        if not attributions:
            lines.append("This module source has no qt_attribution.json records.")
        for path, records in sorted(attributions.items()):
            lines.append(path)
            for record in records:
                for key, value in record.items():
                    lines.append(f"{key}: {value}")
                lines.append("")
        notices["THIRD-PARTY-NOTICES.txt"] = "\n".join(lines).encode()
    if "SQLite-PUBLIC-DOMAIN.txt" in notices:
        # The other license files in this archive belong to its build tools.
        return {"SQLite-PUBLIC-DOMAIN.txt": notices["SQLite-PUBLIC-DOMAIN.txt"]}
    return notices


def collect(app, roots, cache):
    # Modern Homebrew qt is a meta-package. Use the runtime modules OpenSCAD
    # deploys rather than collecting every optional Qt module and tool.
    if "qt" in roots:
        qt_url, _ = source_info(Path(run("brew", "--prefix", "qt")).resolve())
        if qt_url.endswith("sums.txt"):
            roots = [root for root in roots if root != "qt"] + sorted(QT_MODULES)
    # Use installed runtime receipts, not today's formula definitions. For
    # example, an older Qt can still need icu4c@77 alongside Boost's icu4c@78.
    kegs, pending = {}, list(roots)
    while pending:
        formula = pending.pop()
        # Homebrew's CA store is external data; macdeployqt does not bundle it.
        if formula in kegs or formula == "ca-certificates":
            continue
        keg = Path(run("brew", "--prefix", formula)).resolve()
        receipt = json.loads((keg / "INSTALL_RECEIPT.json").read_text())
        kegs[formula] = keg
        pending.extend(dep["full_name"] for dep in receipt.get("runtime_dependencies", []))
    destination = app / "Contents/Resources/licenses/Homebrew"
    staging = cache / "staging"
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)
    index = ["Homebrew runtime dependencies", "",
             "Collected from the installed build dependencies and their runtime closure.",
             "Some platform alternatives may not be present in this app.", ""]
    for keg in sorted(set(kegs.values())):
        url, checksum = source_info(keg)
        label = f"{keg.parent.name}/{keg.name}"
        print(f"Collecting {label}", flush=True)
        files = {str(p.relative_to(keg)): p.read_bytes() for p in installed_notices(keg)}
        # Qt and Scintilla contain additional embedded contributions that are not
        # covered by the single top-level license installed by their formulae.
        qt = keg.parent.name == "qt" or keg.parent.name.startswith("qt@") or keg.parent.name in (
            QT_MODULES | {"qtshadertools", "qttools", "qtwebchannel", "qtpositioning"})
        if not files or qt or keg.parent.name in {"qscintilla2", "lib3mf"}:
            files.update({"source/" + p: data for p, data in
                          source_notices(download(url, checksum, cache), qt,
                                         "qt-everywhere" in url).items()})
        index.extend([label, f"Source: {url}", f"Source SHA256: {checksum}", "Notices:"])
        for relative, data in sorted(files.items()):
            if not data.strip():
                raise RuntimeError(f"Empty notice: {label}/{relative}")
            target = staging / label / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            index.append(f"  {label}/{relative}")
        index.append("")
    (staging / "README.txt").write_text("\n".join(index))
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(staging, destination)
    print(f"Collected {len(set(kegs.values()))} runtime dependency notice sets into {destination}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    parser.add_argument("--formula", action="append", help="Override root formulae (repeatable)")
    parser.add_argument("--cache", type=Path, default=Path("deps/notice-sources"))
    args = parser.parse_args()
    if not (args.app / "Contents/MacOS/OpenSCAD").is_file():
        parser.error("Expected OpenSCAD.app")
    collect(args.app.resolve(), args.formula or FORMULAE, args.cache.resolve())


if __name__ == "__main__":
    main()
