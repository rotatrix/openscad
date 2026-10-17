"""Keep MSYS2 notices for DLLs shipped in the portable package (run in MSYS2)."""
from pathlib import Path
import shutil
import re
import subprocess
import sys


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
    for owner in sorted(owners):
        metadata = subprocess.check_output(['pacman', '-Qi', owner], text=True)
        sources.append(metadata)
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
    (destination / 'MSYS2-packages.txt').write_text(
        'Bundled DLL package versions and upstream URLs follow. MSYS2 packaging recipes:\n'
        'https://github.com/msys2/MINGW-packages\n\n' + '\n'.join(sources), encoding='utf-8')


if __name__ == '__main__':
    collect(Path(sys.argv[1]), Path(sys.argv[2]))
