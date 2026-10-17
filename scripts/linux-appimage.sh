#!/usr/bin/env bash
# Based on .circleci/config.yml openscad-appimage at upstream c2c321215e67.
# Run inside scripts/docker/appimage-base/Dockerfile's appimage-base target.
# Select workspace-local CMake >=3.24 through CMAKE or PATH (Ubuntu 22.04 ships 3.22).
# Stages: build (incremental), package, smoke [AppImage], all (default).
# Cache the Docker layers, build-appimage, and optionally SCCACHE_DIR. Never
# share a build directory between different architectures or toolchains.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build=${OPENSCAD_APPIMAGE_BUILD_DIR:-$root/build-appimage}
artifacts=${OPENSCAD_APPIMAGE_ARTIFACT_DIR:-$root/artifacts/appimage}
cmake=${CMAKE:-cmake}
jobs=${CMAKE_BUILD_PARALLEL_LEVEL:-2}
commit=${OPENSCAD_COMMIT:-$(git -c safe.directory="$root" -C "$root" rev-parse HEAD)}
version=${OPENSCAD_VERSION:-$(date -u +%Y.%m.%d)}
mkdir -p "$build" "$artifacts"
build=$(realpath "$build")
artifacts=$(realpath "$artifacts")
appdir=$build/AppDir

build_app() {
  local launchers=(-DCMAKE_C_COMPILER_LAUNCHER= -DCMAKE_CXX_COMPILER_LAUNCHER=)
  if command -v sccache >/dev/null; then
    sccache --start-server
    launchers=(-DCMAKE_C_COMPILER_LAUNCHER=sccache -DCMAKE_CXX_COMPILER_LAUNCHER=sccache)
  fi
  "$cmake" -S "$root" -B "$build/compile" \
    -DOPENSCAD_VERSION="$version" -DOPENSCAD_COMMIT="$commit" \
    -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release \
    -DUSE_BUILTIN_OPENCSG=ON -DMANIFOLD_PYBIND=OFF -DMANIFOLD_TEST=OFF \
    -DENABLE_PYTHON=ON -DENABLE_TESTS=OFF -DUSE_QT6=ON \
    -DSNAPSHOT=ON -DEXPERIMENTAL=ON -DENABLE_OPENAXIS=ON "${launchers[@]}"
  "$cmake" --build "$build/compile" --parallel "$jobs"
  if command -v sccache >/dev/null; then sccache --show-stats; fi
}

package_app() {
  local built_commit
  built_commit=$(sed -n 's/^OPENSCAD_COMMIT:[^=]*=//p' "$build/compile/CMakeCache.txt")
  [[ $built_commit == "$commit" ]] || { echo 'Build this source revision before packaging' >&2; return 1; }
  # A fresh package tree avoids retaining removed resources from earlier builds.
  rm -rf -- "$appdir"
  DESTDIR="$appdir" "$cmake" --install "$build/compile"
  export PATH=/appimage/usr/bin:"$PATH"
  export EXTRA_QT_PLUGINS=svg QMAKE=/usr/bin/qmake6
  export PYTHON_VERSION=$(python3 -c 'import sys; print(".".join(map(str, sys.version_info[:3])))')
  export PYTHON_BUILD_DIR="$build/python" NPROC="$jobs"
  export LINUXDEPLOY_OUTPUT_VERSION="$version-$commit"
  # Deploy before creating the final AppImage so bundled-library notices enter it.
  (cd "$build" && linuxdeploy --plugin python --plugin qt --appdir "$appdir")
  python3 - "$appdir" "$PYTHON_BUILD_DIR" <<'PY'
import pathlib, shutil, subprocess, sys
app, python_build = map(pathlib.Path, sys.argv[1:])
notices = app / 'usr/share/openscad/licenses'
notices.mkdir(parents=True, exist_ok=True)
# Libraries built by the upstream Dockerfile have no dpkg owner.
custom = pathlib.Path('/usr/share/openscad-dependency-notices')
if not custom.is_dir():
    raise SystemExit('Rebuild the AppImage base image: source dependency notices missing')
shutil.copytree(custom, notices / 'Source-dependencies', dirs_exist_ok=True)
packages = {'libeigen3-dev'}  # compiled headers, not a deployed shared library
for library in (app / 'usr').rglob('*.so*'):
    if not library.is_file():
        continue
    owners = subprocess.run(['dpkg-query', '-S', '*/' + library.name], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    for line in owners.stdout.splitlines():
        if ': /' in line:
            packages.add(line.split(': /', 1)[0])
for package in sorted(packages):
    name = package.split(':')[0]
    copyright = pathlib.Path('/usr/share/doc') / name / 'copyright'
    if not copyright.is_file():
        raise SystemExit(f'Missing installed copyright: {package}')
    dest = notices / 'Ubuntu' / name
    dest.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(copyright, dest / 'copyright')
    source = subprocess.check_output(['dpkg-query', '-W', '-f=${source:Package} ${source:Version}\n', package], text=True)
    (dest / 'source.txt').write_text(source)
# Ubuntu copyright files may refer to /usr/share/common-licenses by name.
shutil.copytree('/usr/share/common-licenses', notices / 'Ubuntu/common-licenses', dirs_exist_ok=True)
licenses = list(python_build.glob('Python-*/LICENSE'))
if len(licenses) != 1:
    raise SystemExit('Expected the deployed Python source license')
python_notices = notices / 'Python'
python_notices.mkdir(exist_ok=True)
shutil.copyfile(licenses[0], python_notices / 'LICENSE')
shutil.copyfile(licenses[0].parent / 'Doc/license.rst', python_notices / 'license.rst')
PY
  # The upstream Python plugin deploys the interpreter but does not select its
  # home for embedded Python. A build host's /usr Python otherwise hides this.
  rm -f -- "$appdir/AppRun"
  cat > "$appdir/AppRun" <<'APPRUN'
#!/bin/sh
APPDIR=$(CDPATH= cd -- "$(dirname -- "$(readlink -f -- "$0")")" && pwd)
export APPDIR
export PYTHONHOME="$APPDIR/usr"
unset PYTHONPATH
exec "$APPDIR/usr/bin/openscad" "$@"
APPRUN
  chmod +x "$appdir/AppRun"
  (cd "$build" && linuxdeploy --appdir "$appdir" --output appimage)
  # Snapshot desktop names may add a Nightly suffix; select only this exact version.
  local matches=("$build"/*"$LINUXDEPLOY_OUTPUT_VERSION"*-x86_64.AppImage)
  [[ ${#matches[@]} == 1 && -f ${matches[0]} ]] || { echo 'Expected one AppImage' >&2; return 1; }
  cp -- "${matches[0]}" "$artifacts/openscad-linux-x86_64-qt6-$commit.AppImage"
  (cd "$artifacts" && sha256sum "openscad-linux-x86_64-qt6-$commit.AppImage" > "openscad-linux-x86_64-qt6-$commit.AppImage.sha256")
}

smoke_app() {
  local package=${1:-$artifacts/openscad-linux-x86_64-qt6-$commit.AppImage}
  package=$(realpath "$package")
  local smoke
  smoke=$(mktemp -d "$build/smoke.XXXXXX")
  # Extraction tests the delivered payload without requiring host FUSE setup.
  (cd "$smoke" && "$package" --appimage-extract > extract.log)
  local app="$smoke/squashfs-root/AppRun"
  ln -s "$app" "$smoke/AppRun link"
  "$smoke/AppRun link" --version > "$smoke/symlink.log" 2>&1
  printf 'difference() { cube(10, center=true); sphere(6, $fn=32); }\n' > "$smoke/model with spaces.scad"
  "$app" --info > "$smoke/info.log" 2>&1
  grep -F "$commit" "$smoke/info.log"
  grep -F 'python-engine' "$smoke/info.log"
  for backend in CGAL Manifold; do
    "$app" --backend="$backend" -o "$smoke/$backend.stl" "$smoke/model with spaces.scad"
    test -s "$smoke/$backend.stl"
  done
  "$app" -o "$smoke/model.3mf" "$smoke/model with spaces.scad"
  "$app" --render -o "$smoke/render.png" "$smoke/model with spaces.scad"
  test -s "$smoke/model.3mf" && test -s "$smoke/render.png"
  printf 'import os, sys\nassert os.path.realpath(sys.prefix) == os.path.realpath(os.path.join(os.environ["APPDIR"], "usr"))\nassert not os.environ.get("PYTHONPATH")\nfrom openscad import cube, show\nshow(cube(3))\n' > "$smoke/model with spaces.py"
  "$app" --trust-python --enable=python-engine -o "$smoke/python.stl" "$smoke/model with spaces.py"
  test -s "$smoke/python.stl"
  PYTHONHOME="$smoke/not-the-package" PYTHONPATH="$smoke/not-the-package" \
    "$app" --trust-python --enable=python-engine -o "$smoke/python-host-env.stl" "$smoke/model with spaces.py"
  test -s "$smoke/python-host-env.stl"
  test -f "$smoke/squashfs-root/usr/share/openscad/licenses/OpenAxis/LICENSE"
  # Optional on non-desktop machines; the builder image includes xvfb-run.
  if command -v xvfb-run >/dev/null; then
    local status=0
    XDG_CONFIG_HOME="$smoke/config" XDG_CACHE_HOME="$smoke/cache" \
      timeout 10s xvfb-run -a "$app" "$smoke/model with spaces.scad" > "$smoke/gui.log" 2>&1 || status=$?
    [[ $status == 124 ]] || { cat "$smoke/gui.log"; return 1; }
  fi
  chmod -R a+rX "$smoke"
  echo "Package smoke results: $smoke"
}

case ${1:-all} in
  build) build_app ;;
  package) package_app ;;
  smoke) smoke_app "${2:-}" ;;
  all) build_app; package_app; smoke_app ;;
  *) echo "Usage: $0 {build|package|smoke [AppImage]|all}" >&2; exit 2 ;;
esac
