"""Keep MSYS2 notices for DLLs shipped in the portable package (run in MSYS2)."""
from pathlib import Path
import hashlib
import importlib.util
import shutil
import re
import subprocess
import sys

spec = importlib.util.spec_from_file_location('notices', Path(__file__).with_name('macos-dependency-notices.py'))
notices = importlib.util.module_from_spec(spec)
spec.loader.exec_module(notices)


def collect(package, destination):
    msys_root = Path(subprocess.check_output(['cygpath', '-m', '/'], text=True).strip())
    dlls = {p.name.lower() for p in package.rglob('*') if p.suffix.lower() == '.dll'}
    listing = subprocess.check_output(['pacman', '-Ql'], text=True).splitlines()
    files = [line.split(' ', 1) for line in listing]
    owners = {owner for owner, path in files if Path(path).name.lower() in dlls}
    if not owners:
        raise RuntimeError('No MSYS2 owners found for packaged DLLs')
    destination.mkdir(parents=True, exist_ok=True)
    sources = []
    source_sets = {}
    for owner in sorted(owners):
        metadata = subprocess.check_output(['pacman', '-Qi', owner], text=True)
        sources.append(metadata)
        component_name = owner.removeprefix('mingw-w64-ucrt-x86_64-')
        version = re.search(r'^Version\s+:\s+([0-9.]+)-', metadata, re.MULTILINE)
        if component_name.startswith('qt6-') and not component_name.endswith('-ffmpeg'):
            if not version:
                raise RuntimeError('Cannot identify exact Qt source version: ' + owner)
            module = 'qt' + component_name[4:]
            number = version[1]
            url = (f'https://download.qt.io/official_releases/qt/{number.rsplit(".", 1)[0]}/{number}'
                   f'/submodules/{module}-everywhere-src-{number}.tar.xz')
            checksum = subprocess.check_output(['curl', '-fsSL', url + '.sha256'], text=True).split()[0]
            if not re.fullmatch('[0-9a-f]{64}', checksum):
                raise RuntimeError('Invalid Qt source checksum')
            source_sets[component_name] = (url, checksum, True)
        elif component_name.startswith('qscintilla-'):
            if not version or version[1] != '2.14.1':
                raise RuntimeError('Review the source checksum for the new QScintilla version')
            source_sets['qscintilla'] = (
                'https://www.riverbankcomputing.com/static/Downloads/QScintilla/2.14.1/QScintilla_src-2.14.1.tar.gz',
                'dfe13c6acc9d85dfcba76ccc8061e71a223957a6c02f3c343b30a9d43a4cdd4d', False)
        for component, path in files:
            source = msys_root / path.lstrip('/')
            is_notice = '/share/licenses/' in path or (
                '/share/' in path and re.match(r'^(license|licence|copying|copyright|notice)([._-].*)?$',
                                               source.name, re.IGNORECASE))
            if component != owner or not is_notice or not source.is_file():
                continue
            relative = (path.split('/share/licenses/', 1)[1] if '/share/licenses/' in path
                        else owner.removeprefix('mingw-w64-ucrt-x86_64-') + '/' + source.name)
            output = destination / relative
            output.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, output)
    # Qt's SPDX templates omit embedded third-party copyrights, and the
    # QScintilla package omits Scintilla notices. Reuse the macOS source reader.
    for component, (url, checksum, qt) in sorted(source_sets.items()):
        archive = notices.download(url, checksum, Path('deps/notice-sources'))
        files = notices.source_notices(archive, qt=qt, monolithic=False)
        folder = destination / component / 'source'
        folder.mkdir(parents=True, exist_ok=True)
        index = [f'Source: {url}', f'SHA256: {checksum}', '']
        for relative, data in sorted(files.items()):
            # Keep Windows archive paths short while retaining source mapping.
            filename = hashlib.sha256(data).hexdigest() + '.txt'
            (folder / filename).write_bytes(data)
            index.append(f'{filename}  {relative}')
        (folder / 'SOURCES.txt').write_text('\n'.join(index) + '\n', encoding='utf-8')
    (destination / 'MSYS2-packages.txt').write_text(
        'Bundled DLL package versions and upstream URLs follow. MSYS2 packaging recipes:\n'
        'https://github.com/msys2/MINGW-packages\n\n' + '\n'.join(sources), encoding='utf-8')


if __name__ == '__main__':
    collect(Path(sys.argv[1]), Path(sys.argv[2]))
